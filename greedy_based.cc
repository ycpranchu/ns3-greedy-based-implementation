#include <stack>
#include <vector>
#include <algorithm>
#include <fstream>
#include <random>
#include <iostream>
#include <iterator>
#include <sstream>

#include "ns3/core-module.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/mobility-model.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"
#include "ns3/ns2-mobility-helper.h"

using namespace ns3;

// Define a Log component with a specific name.
NS_LOG_COMPONENT_DEFINE("DTN-EPIDEMIC");

// Define enumeration for PayLoad type.
enum
{
    EPIDEMIC
};

// Define the data struct of packet named 'PacketLogData'.
typedef struct
{
    bool delivered;
    double start;
    double delivered_at;
    double ttl;
} PacketLogData;

std::string debugLevel = "MAX"; //["NONE","NORMAL","MAX","EXTRACTOR"]
std::vector<PacketLogData> dataForPackets;
std::vector<std::vector<std::string>> existNode;
NodeContainer c;
double interval = 0.1;

// Use the delimiter to split string.
std::vector<std::string> splitString(std::string value, std::string delimiter)
{
    std::vector<std::string> values;
    size_t pos = 0;
    std::string token;
    while ((pos = value.find(delimiter)) != std::string::npos)
    {
        token = value.substr(0, pos);
        values.push_back(token);
        value.erase(0, pos + delimiter.length());
    }
    values.push_back(value);
    return values;
}

// Use the delimiter to create string.
std::string createStringAddressUid(Ipv4Address address, int uid, std::string delimiter)
{
    std::ostringstream value;
    value << address << delimiter << uid;
    return value.str();
}

class PayLoadConstructor
{
private:
    int type; // routing type
    uint32_t ttl;
    uint32_t uid;
    Ipv4Address destinationAddress;
    std::string delimiter;

public:
    PayLoadConstructor(int _type)
    {
        delimiter = ";";
        type = _type;
    }

    // Get function.
    uint32_t getTtl() { return ttl; };
    uint32_t getUid() { return uid; };
    Ipv4Address getDestinationAddress() { return destinationAddress; };

    // Set function.
    void setTtl(uint32_t value) { ttl = value; };
    void setUid(uint32_t value) { uid = value; };
    void setDestinationAddress(Ipv4Address value) { destinationAddress = value; };
    void setDestinationAddressFromString(std::string value) { destinationAddress = ns3::Ipv4Address(value.c_str()); };

    // void decreaseTtl() { ttl -= 1; };

    void fromString(std::string stringPayload)
    {
        if (type == EPIDEMIC)
        {
            // 10.0.2.3;5;3 => IP;TTL;UID(packet ID)
            std::vector<std::string> values = splitString(stringPayload, delimiter);
            destinationAddress = ns3::Ipv4Address(values[0].c_str());
            ttl = std::stoi(values[1]);
            uid = std::stoi(values[2]);
        }
    }

    void fromPacket(Ptr<Packet> packet)
    {
        uint8_t *buffer = new uint8_t[packet->GetSize()]; // size = packet->GetSize()
        packet->CopyData(buffer, packet->GetSize());
        std::string stringPayload = std::string((char *)buffer);

        fromString(stringPayload);
    };

    std::ostringstream toString()
    {
        std::ostringstream msg;
        msg << destinationAddress << delimiter << ttl << delimiter << uid;
        return msg;
    };

    Ptr<Packet> toPacket()
    {
        std::ostringstream msg = toString();
        uint32_t packetSize = msg.str().length() + 1;
        Ptr<Packet> packet = Create<Packet>((uint8_t *)msg.str().c_str(), packetSize);
        return packet;
    }
};

class NodeHandler
{
private:
    double bytesSent;
    int packetsSent;
    double bytesReceived;
    int packetsReceived;
    int attempt;
    double bufferSize;
    double maxBufferSize;
    double packetSize;
    std::stack<uint64_t> packetsScheduled;
    std::stack<std::string> uidsPacketReceived;

public:
    NodeHandler()
    {
        bytesSent = 0.00;
        packetsSent = 0;
        bytesReceived = 0.0;
        packetsReceived = 0;
        attempt = 0;
        bufferSize = 0;
        maxBufferSize = 5000000;
        packetSize = 1024;
    }
    double getBytesSent() { return bytesSent; }
    int getPacketsSent() { return packetsSent; }

    void setBytesSent(double value) { bytesSent = value; }
    void setPacketsSent(double value) { packetsSent = value; }

    double getBytesReceived() { return bytesReceived; }
    int getPacketsReceived() { return packetsReceived; }

    void incrementAttempt() { attempt++; }
    int getAttempt() { return attempt; }

    void setBytesReceived(double value) { bytesReceived = value; }
    void setPacketsReceived(double value) { packetsReceived = value; }

    void increaseBytesSent() { bytesSent += packetSize; }
    void increasePacketsSent(double value) { packetsSent += value; }
    void increaseBytesReceived() { bytesReceived += packetSize; }
    void increasePacketsReceived(double value) { packetsReceived += value; }

    void increaseBuffer() { bufferSize += packetSize; }
    void decreaseBuffer() { bufferSize -= packetSize; }

    bool checkBufferSize()
    {
        if (bufferSize + packetSize > maxBufferSize)
            return false;
        else
            return true;
    }

    bool searchInStack(uint64_t value)
    {
        std::stack<uint64_t> s = packetsScheduled;
        // stack is not empty
        while (!s.empty())
        {
            uint64_t top = s.top();
            if (value == top)
                return true;
            s.pop();
        }
        return false;
    }

    int countInReceived(std::string value)
    {
        // 10.0.1.2;5 => IP;UID
        std::vector<std::string> values = splitString(value, ";");
        int uid = std::stoi(values[1]);

        std::stack<std::string> s = uidsPacketReceived;

        int counter = 0;
        while (!s.empty())
        {
            std::string top = s.top();
            values = splitString(top, ";");
            int tempUid = std::stoi(values[1]);

            if (uid == tempUid)
                counter++;
            s.pop();
        }

        return counter;
    }

    bool searchInReceived(std::string value)
    {
        std::stack<std::string> s = uidsPacketReceived;

        while (!s.empty())
        {
            std::string top = s.top();
            if (top == value)
                return true;
            s.pop();
        }
        return false;
    }

    void pushInStack(uint64_t value) { packetsScheduled.push(value); }

    std::string pushInReceived(ns3::Ipv4Address previousAddress, int uid)
    {
        std::string value = createStringAddressUid(previousAddress, uid, ";");
        uidsPacketReceived.push(value);
        return value;
    }

    void popFromStack() { packetsScheduled.pop(); }
    void popFromReceived() { uidsPacketReceived.pop(); }
};

std::vector<NodeHandler> nodeHandlerArray;

static void GenerateTraffic(Ptr<Socket> socket, Ptr<Packet> packet, uint32_t UID, std::string previousAddressUid, uint32_t ttl)
{
    Ptr<Ipv4> ipv4 = socket->GetNode()->GetObject<Ipv4>();
    Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
    Ipv4Address ipSender = iaddr.GetLocal();

    NodeHandler *currentNode = &nodeHandlerArray[socket->GetNode()->GetId()];

    if (currentNode->searchInStack(UID) == false ||              // stack of sent pkt
        (currentNode->searchInStack(UID) == true &&              // stack of sent pkt
         (currentNode->countInReceived(previousAddressUid) < 2)) // stack of received pkt
    )
    {
        NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t" << ipSender << " " << socket->GetNode()->GetId() << " going to send packet with uid: " << UID);
        socket->Send(packet);

        if (dataForPackets[UID].start == -1)
            dataForPackets[UID].start = Simulator::Now().GetSeconds();

        if (currentNode->searchInStack(UID) == false)
            currentNode->pushInStack(UID);

        currentNode->increaseBytesSent();
        currentNode->increasePacketsSent(1);
        currentNode->decreaseBuffer();

        Simulator::Schedule(Seconds(interval), &GenerateTraffic, socket, packet, UID, previousAddressUid, ttl);
    }
}

void ReceivePacket(Ptr<Socket> socket)
{
    Address from;
    Ipv4Address ipSender;
    Ptr<Packet> pkt;

    while (pkt = socket->RecvFrom(from))
    {
        NodeHandler *currentNode = &nodeHandlerArray[socket->GetNode()->GetId()];
        ipSender = InetSocketAddress::ConvertFrom(from).GetIpv4();

        currentNode->increasePacketsReceived(1);

        Ptr<Ipv4> ipv4 = socket->GetNode()->GetObject<Ipv4>();
        Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
        Ipv4Address ipReceiver = iaddr.GetLocal();

        PayLoadConstructor payload = PayLoadConstructor(EPIDEMIC);
        payload.fromPacket(pkt);

        uint32_t UID = payload.getUid();
        uint32_t TTL = payload.getTtl();

        Ipv4Address destinationAddress = payload.getDestinationAddress();
        std::string previousAddressUid = createStringAddressUid(ipSender, (int)UID, ";");

        if (currentNode->searchInReceived(previousAddressUid) == false) 
        {
            currentNode->pushInReceived(ipSender, UID);
            currentNode->increaseBytesReceived();
            currentNode->increaseBuffer();
        }

        double time = Simulator::Now().GetSeconds();
        NS_LOG_UNCOND(time << "s\t" << ipReceiver << "    " << socket->GetNode()->GetId() << "    Received pkt size: " << pkt->GetSize() << " bytes with uid    " << UID << "    and TTL    " << TTL << "    from: " << ipSender << " to: " << destinationAddress);

        if (ipReceiver != destinationAddress)
        {
            bool exist = false;

            for (std::vector<std::string>::iterator iter = existNode[(int)time].begin(); iter != existNode[(int)time].end(); iter++)
            {
                if ((int)socket->GetNode()->GetId() == stoi(*iter))
                {
                    exist = true; // check node exist
                    break;
                }
            }

            if ((dataForPackets[UID].start + (double)TTL >= Simulator::Now().GetSeconds()) && currentNode->checkBufferSize() && exist == true)
            {
                if (currentNode->searchInStack(UID) == false)
                {
                    InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
                    socket->SetAllowBroadcast(true);
                    socket->Connect(remote);

                    Ptr<Packet> packet = payload.toPacket();
                    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t" << ipReceiver << "    " << socket->GetNode()->GetId() << "\tGoing to RE-send packet with uid: " << UID);

                    // Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
                    // double randomPause = x->GetValue(0.1, 1.0);

                    Simulator::Schedule(Seconds(interval), &GenerateTraffic, socket, packet, UID, previousAddressUid, TTL);
                }
                else
                {
                    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t" << ipReceiver << "\tI've already scheduled the message with uid: " << UID);
                }
            }
        }
        else
        {
            // arrive to the DESTINATION
            if (dataForPackets[UID].delivered != true)
            {
                dataForPackets[UID].ttl = TTL;
                dataForPackets[UID].delivered = true;
                dataForPackets[UID].delivered_at = Simulator::Now().GetSeconds();

                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s I am " << ipReceiver << " finally received the package with uid: " << UID);
            }
            else
            {
                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s I am " << ipReceiver << " RE-received the package with uid: " << UID);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    std::string phyMode("DsssRate11Mbps");
    double distance = 600;
    interval = 1;

    // double simulationTime = 569.00;
    double simulationTime = 40.00;
    double sendUntil = 20.00;
    uint32_t seed = 91;

    uint32_t numPair = 50;
    uint32_t numNodes = 3214;
    uint32_t sendAfter = 1;
    uint32_t sinkNode;
    uint32_t sourceNode;

    uint32_t TTL = 50;
    uint32_t UID = 0;

    CommandLine cmd;
    cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
    // cmd.AddValue ("distance", "distance (m)", distance);
    cmd.AddValue("numPair", "Number of packets generated", numPair);
    cmd.AddValue("numNodes", "Number of nodes", numNodes);
    // cmd.AddValue("sinkNode", "Receiver node number", sinkNode);
    cmd.AddValue("sourceNode", "Sender node number", sourceNode);
    cmd.AddValue("ttl", "TTL For each packet", TTL);
    cmd.AddValue("seed", "Custom seed for simulation", seed);
    cmd.AddValue("simulationTime", "Set a custom time (s) for simulation", simulationTime);
    // cmd.AddValue("sendAfter", "Send the first pkt after", sendAfter);

    // cmd.AddValue("rss", "received signal strength", rss);
    cmd.Parse(argc, argv);

    // Fix non-unicast data rate to be the same as that of unicast
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(phyMode));

    c.Create(numNodes);

    // import exist file
    std::string tempstr;
    std::ifstream file;

    file.open("/home/ycpin/exist_test.txt", std::ios::in);
    while (getline(file, tempstr))
    {
        std::stringstream ss(tempstr);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string> tokens(begin, end);

        for (uint32_t i = 0; i < numPair * 2; ++i) {
            tokens.push_back(std::to_string(i));
        }

        // for (std::vector<std::string>::iterator iter = tokens.begin(); iter != tokens.end(); iter++)
        //     std::cout << *iter << " ";
        // std::cout << std::endl;

        existNode.push_back(tokens);
    }
    file.close();

    SeedManager::SetSeed(seed);

    // The below set of helpers will help us to put together the wifi NICs we want
    WifiHelper wifi;
    YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
    // FROM WIFI SIMPLE ADHOC GRID

    // set it to zero; otherwise, gain will be added
    wifiPhy.Set("RxGain", DoubleValue(4));
    wifiPhy.Set("TxGain", DoubleValue(4));
    // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(distance));
    wifiPhy.SetChannel(wifiChannel.Create());

    // Add an upper mac and disable rate control
    WifiMacHelper wifiMac;
    wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue(phyMode),
                                 "ControlMode", StringValue(phyMode));
    // Set it to adhoc mode
    wifiMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);

    Ns2MobilityHelper ns2 = Ns2MobilityHelper("/home/ycpin/mobility_test.tcl");
    ns2.Install(); // configure movements for each node, while reading trace file

    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;
    NS_LOG_INFO("Assign IP Addresses.");
    ipv4.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer i = ipv4.Assign(devices);

    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);

    Ptr<Socket> recvSinkArray[numNodes];
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        nodeHandlerArray.push_back(*new NodeHandler());
        recvSinkArray[i] = Socket::CreateSocket(c.Get(i), tid);
        recvSinkArray[i]->Bind(local);
        recvSinkArray[i]->SetRecvCallback(MakeCallback(&ReceivePacket));
    }

    for (double t = 0; t < simulationTime - sendUntil; t += sendAfter)
    {
        for (uint32_t i = 0; i < numPair; i++)
        {
            sinkNode = i * 2;
            sourceNode = i * 2 + 1;

            // destination node
            Ipv4InterfaceAddress iaddr = c.Get(sinkNode)->GetObject<Ipv4>()->GetAddress(1, 0);
            Ipv4Address ipReceiver = iaddr.GetLocal();

            // source node
            Ipv4InterfaceAddress iaddrSender = c.Get(sourceNode)->GetObject<Ipv4>()->GetAddress(1, 0);
            Ipv4Address ipSender = iaddrSender.GetLocal();

            // broadcast
            Ptr<Socket> source = Socket::CreateSocket(c.Get(sourceNode), tid);
            InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
            source->SetAllowBroadcast(true);
            source->Connect(remote);

            PayLoadConstructor payload = PayLoadConstructor(EPIDEMIC);
            payload.setTtl(TTL);
            payload.setUid(UID);
            payload.setDestinationAddress(ipReceiver);
            Ptr<Packet> packet = payload.toPacket();

            PacketLogData dataPacket = {false, -1, 0.00, 0};
            dataForPackets.push_back(dataPacket);

            Simulator::Schedule(Seconds(t), &GenerateTraffic, source, packet, UID, createStringAddressUid(ipSender, (int)UID, ";"), TTL);
            UID += 1;
        }
    }

    AnimationInterface anim("epidemic-anim.xml");

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    // simulator is ending

    int deliveredCounter = 0;
    double end2endDelay = 0.0;

    for (int i = 0; i < (int)dataForPackets.size(); i++)
    {
        if (dataForPackets[i].delivered == true)
        {
            deliveredCounter++;
            end2endDelay += (double)(dataForPackets[i].delivered_at - dataForPackets[i].start);

            if (debugLevel != "NONE")
            {
                NS_LOG_UNCOND("- Packets " << i + 1 << " delta delivery: \t" << (double)(dataForPackets[i].delivered_at - dataForPackets[i].start));
                NS_LOG_UNCOND("- Packets " << i + 1 << " End-to-End Delay: \t" << (double)(dataForPackets[i].delivered_at - dataForPackets[i].start));
            }
        }
        else if (debugLevel != "NONE")
        {
            NS_LOG_UNCOND("- Packets " << i + 1 << " delta delivery: \t" << 0);
            NS_LOG_UNCOND("- Packets " << i + 1 << " TTL/HOPS: \t" << 0);
        }
    }
    if (debugLevel != "NONE")
    {
        NS_LOG_UNCOND("- Packets sent: \t" << (int)dataForPackets.size());
        NS_LOG_UNCOND("- Packets delivered: \t" << deliveredCounter);
        NS_LOG_UNCOND("- Delivery percentage: \t" << ((double)deliveredCounter / (double)dataForPackets.size()) * 100.00 << "%");
        // Delivery time (?) (?) (?)
    }

    double totalBytesSent = 0.00;
    double totalBytesReceived = 0.00;
    int totalPacketsSent = 0;
    int totalPacketsReceived = 0;

    for (uint32_t i = 0; i < numNodes; ++i)
    {
        totalBytesSent += nodeHandlerArray[i].getBytesSent();
        totalBytesReceived += nodeHandlerArray[i].getBytesReceived();
        totalPacketsSent += nodeHandlerArray[i].getPacketsSent();
        totalPacketsReceived += nodeHandlerArray[i].getPacketsReceived();
    }

    if (debugLevel != "NONE")
    {
        NS_LOG_UNCOND("- Total BytesSent: \t" << totalBytesSent);
        NS_LOG_UNCOND("- Total BytesReceived: \t" << totalBytesReceived);
        NS_LOG_UNCOND("- Total PacketsSent: \t" << totalPacketsSent);
        NS_LOG_UNCOND("- Total PacketsReceived: \t" << totalPacketsReceived);
        NS_LOG_UNCOND("- Average End-to-End Delay: \t" << end2endDelay / deliveredCounter);
    }
    return 0;
}
