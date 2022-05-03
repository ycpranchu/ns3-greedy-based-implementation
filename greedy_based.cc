#include <stack>
#include <vector>
#include <algorithm>
#include <fstream>
#include <random>
#include <iostream>
#include <iterator>
#include <sstream>
#include <math.h>

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
NS_LOG_COMPONENT_DEFINE("DQG-GREEDY_BASED");

// Define enumeration for PayLoad type
enum
{
    HELLO,
    HELLO_ACK,
    HELLO_ACK2,
};

typedef struct
{
    bool delivered;
    double start;
    double delivered_at;
    int ttl;
} PacketLogData;

std::string debugLevel = "NORMAL"; //["NONE", "NORMAL", "MAX", "EXTRACTOR"]
std::vector<PacketLogData> dataForPackets;
std::vector<std::vector<std::string>> existNode;
NodeContainer c;
double interval = 0.1;

uint32_t gridSize = 1000;
uint32_t BOARD_ROWS = 4;
uint32_t BOARD_COLS = 6;
std::string nextHopGrid[24][24][4];

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

std::string createStringAddressUid(Ipv4Address address, int uid, std::string delimiter)
{
    std::ostringstream value;
    value << address << delimiter << uid;
    return value.str();
}

class PayLoadConstructor
{
private:
    int type;
    uint32_t destinationId;
    uint32_t ttl;
    uint32_t uid;
    Ipv4Address neighbor;
    float x = 0;
    float y = 0;
    Ipv4Address destinationAddress;
    std::string delimiter;

public:
    PayLoadConstructor(int _type)
    {
        delimiter = ";";
        type = _type;
    }

    uint32_t getTtl() { return ttl; };
    uint32_t getUid() { return uid; };
    int getType() { return type; };
    Ipv4Address getNeighbor() { return neighbor; };
    double getX() { return x; };
    double getY() { return y; };
    uint32_t getDestinationId() { return destinationId; };
    Ipv4Address getDestinationAddress() { return destinationAddress; };

    void setTtl(uint32_t value) { ttl = value; };
    void setUid(uint32_t value) { uid = value; };
    void setType(int value) { type = value; };
    void setNeighbor(Ipv4Address value) { neighbor = value; };
    void setX(double value) { x = value; };
    void setY(double value) { y = value; };
    void setDestinationId(uint32_t value) { destinationId = value; };
    void setDestinationAddress(Ipv4Address value) { destinationAddress = value; };
    void setDestinationAddressFromString(std::string value) { destinationAddress = ns3::Ipv4Address(value.c_str()); };

    void fromString(std::string stringPayload)
    {
        std::vector<std::string> values = splitString(stringPayload, delimiter);

        type = std::stoi(values[0]);
        destinationAddress = ns3::Ipv4Address(values[1].c_str());
        destinationId = std::stoi(values[2]);
        ttl = std::stoi(values[3]);
        uid = std::stoi(values[4]);
        neighbor = ns3::Ipv4Address(values[5].c_str());
        x = std::atof(values[6].c_str());
        y = std::atof(values[7].c_str());
    }

    void fromPacket(Ptr<Packet> packet)
    {
        uint8_t *buffer = new uint8_t[packet->GetSize()];
        packet->CopyData(buffer, packet->GetSize());
        std::string stringPayload = std::string((char *)buffer);

        fromString(stringPayload);
    };

    std::ostringstream toString()
    {
        std::ostringstream msg;
        msg << getType() << delimiter << destinationAddress << delimiter << destinationId << delimiter << ttl << delimiter << uid << delimiter << neighbor << delimiter << x << delimiter << y;
        return msg;
    };

    Ptr<Packet> toPacket()
    {
        std::ostringstream msg = toString();
        uint32_t packetSize = msg.str().length() + 1;
        Ptr<Packet> packet = Create<Packet>((uint8_t *)msg.str().c_str(), packetSize);
        return packet;
    }

    Ptr<Packet> toPacketFromString(std::ostringstream &tmp)
    {
        std::ostringstream msg;
        msg << getType() << ";" << tmp.str();
        uint32_t packetSize = msg.str().length() + 1;
        Ptr<Packet> packet = Create<Packet>((uint8_t *)msg.str().c_str(), packetSize);
        return packet;
    }
};

class NodeHandler
{
private:
    int nodeid;

    double bytesSent;
    int packetsSent;
    double bytesReceived;
    int packetsReceived;

    double bufferSize;
    double maxBufferSize;
    double packetSize;

    std::stack<uint64_t> packetsScheduled;
    std::stack<std::string> uidsPacketReceived;
    std::vector<PayLoadConstructor> bufferPackets;

public:
    NodeHandler(int _nodeid)
    {
        nodeid = _nodeid;
        bytesSent = 0.00;
        packetsSent = 0;
        bytesReceived = 0.0;
        packetsReceived = 0;

        bufferSize = 0;
        maxBufferSize = 5000000;
        packetSize = 1024;
    }

    int getNodeID() { return nodeid; }
    double getBytesSent() { return bytesSent; }
    int getPacketsSent() { return packetsSent; }
    double getBytesReceived() { return bytesReceived; }
    int getPacketsReceived() { return packetsReceived; }

    void clearPacketsBuffer() { bufferPackets.clear(); }

    void setBytesSent(double value) { bytesSent = value; }
    void setPacketsSent(double value) { packetsSent = value; }
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

    // void setMeet(std::string nodeip) { meeting.push_back(atoi(nodeip)); }
    // void clearMeet(std::string nodeip) { meeting.clear(); }
    // std::vector<Meet> getMeet() { return meeting; }

    bool searchInStack(uint64_t value)
    {
        std::stack<uint64_t> s = packetsScheduled;
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

    void savePacketsInBuffer(PayLoadConstructor payload)
    {
        bufferPackets.push_back(payload);
    }

    std::vector<PayLoadConstructor> getPacketsBuffer()
    {
        return bufferPackets;
    }

    void removePacketFromBufferByIndex(int index)
    {
        bufferPackets.erase(bufferPackets.begin() + index);
    }
};

std::vector<NodeHandler> nodeHandlerArray;

static void GenerateTraffic(Ptr<Socket> socket, Ptr<Packet> packet, uint32_t UID, uint32_t ttl)
{
    Ptr<Ipv4> ipv4 = socket->GetNode()->GetObject<Ipv4>();
    Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
    Ipv4Address ipSender = iaddr.GetLocal();

    NodeHandler *currentNode = &nodeHandlerArray[socket->GetNode()->GetId()];

    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t" << ipSender << " " << socket->GetNode()->GetId() << " going to send packet with uid: " << UID);
    socket->Send(packet);

    if (dataForPackets[UID].start == -1)
        dataForPackets[UID].start = Simulator::Now().GetSeconds();

    // if (currentNode->searchInStack(UID) == false)
    //     currentNode->pushInStack(UID);

    currentNode->increaseBytesSent();
    currentNode->increasePacketsSent(1);
    currentNode->decreaseBuffer();
}

float dist(float x1, float y1, float x2, float y2)
{
    float dist = sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
    return dist;
}

static void ScheduleNeighbor(NodeHandler currentNode, Ptr<Socket> socket, uint32_t destinationId)
{
    std::vector<PayLoadConstructor> bufferPackets = currentNode.getPacketsBuffer();

    Ptr<MobilityModel> current_mob = c.Get(currentNode.getNodeID())->GetObject<MobilityModel>();
    int index_row = current_mob->GetPosition().y / gridSize;
    int index_col = current_mob->GetPosition().x / gridSize;
    bool dst_grid = true;

    Ptr<MobilityModel> dst_mob = c.Get(destinationId)->GetObject<MobilityModel>();
    float dst_X = dst_mob->GetPosition().x;
    float dst_Y = dst_mob->GetPosition().y;
    int dst_row = dst_Y / gridSize;
    int dst_col = dst_X / gridSize;

    std::cout << index_row << " " << index_col << " " << dst_row << " " << dst_col << std::endl;

    for (int i = 0; i < 4; i++)
        if (std::atof(nextHopGrid[index_row * BOARD_COLS + index_col][dst_row * BOARD_COLS + dst_col][i].c_str()) == -1)
            dst_grid = false;

    int first_choice = -1;
    int second_choice = -1;

    if (dst_grid == false)
    {
        float max = -1;
        for (int i = 0; i < 4; i++) // choise neighbor grid with max grid value
        {
            if (std::atof(nextHopGrid[index_row * BOARD_COLS + index_col][dst_row * BOARD_COLS + dst_col][i].c_str()) > max)
            {
                first_choice = i;
                max = std::atof(nextHopGrid[index_row * BOARD_COLS + index_col][dst_row * BOARD_COLS + dst_col][i].c_str());
            }
        }

        max = -1;
        for (int i = 0; i < 4; i++) // neighbor grid with sub-max grid value
        {
            if (i == first_choice)
                continue;

            if (std::atof(nextHopGrid[index_row * BOARD_COLS + index_col][dst_row * BOARD_COLS + dst_col][i].c_str()) > max)
            {
                second_choice = i;
                max = std::atof(nextHopGrid[index_row * BOARD_COLS + index_col][dst_row * BOARD_COLS + dst_col][i].c_str());
            }
        }
    }

    Ipv4Address nextHopAddress, canNextHopAddress;
    Ptr<Packet> nextHopPacket, canNextHopPacket;
    bool check_A = false, check_B = false;
    double distance_A = 100000, distance_B = 100000;
    uint32_t UID;
    uint32_t ttl;

    for (int buffIndex = 0; buffIndex < (int)bufferPackets.size(); buffIndex++)
    {
        PayLoadConstructor payload = bufferPackets[buffIndex];
        Ptr<Packet> packet = payload.toPacket();
        Ipv4Address ipSender = payload.getNeighbor();

        UID = payload.getUid();
        ttl = payload.getTtl();

        if (payload.getDestinationAddress() == ipSender)
        {
            nextHopAddress = ipSender;
            nextHopPacket = packet;
            check_A = true;
            break;
        }
        else
        {
            double node_X = payload.getX();
            double node_Y = payload.getY();
            int node_rowIndex = node_Y / gridSize;
            int node_colIndex = node_X / gridSize;

            if (dst_grid == true)
            {
                if (node_rowIndex == dst_row && node_colIndex == dst_col)
                {
                    int temp_distance = dist(node_X, node_Y, dst_X, dst_Y);

                    if (temp_distance < distance_A)
                    {
                        nextHopAddress = ipSender;
                        nextHopPacket = packet;
                        distance_A = temp_distance;
                        check_A = true;
                    }
                }
            }
            else
            {
                int now_rowIndex;
                int now_colIndex;
                int canNow_rowIndex;
                int canNow_colIndex;

                if (first_choice == 0)
                {
                    now_rowIndex = node_rowIndex - 1;
                    now_colIndex = node_colIndex;
                }
                if (first_choice == 1)
                {
                    now_rowIndex = node_rowIndex + 1;
                    now_colIndex = node_colIndex;
                }
                if (first_choice == 2)
                {
                    now_rowIndex = node_rowIndex;
                    now_colIndex = node_colIndex - 1;
                }
                if (first_choice == 3)
                {
                    now_rowIndex = node_rowIndex;
                    now_colIndex = node_colIndex + 1;
                }

                if (second_choice == 0)
                {
                    canNow_rowIndex = node_rowIndex - 1;
                    canNow_colIndex = node_colIndex;
                }
                if (second_choice == 1)
                {
                    canNow_rowIndex = node_rowIndex + 1;
                    canNow_colIndex = node_colIndex;
                }
                if (second_choice == 2)
                {
                    canNow_rowIndex = node_rowIndex;
                    canNow_colIndex = node_colIndex - 1;
                }
                if (second_choice == 3)
                {
                    canNow_rowIndex = node_rowIndex;
                    canNow_colIndex = node_colIndex + 1;
                }

                if (now_rowIndex == dst_row && now_colIndex == dst_col)
                {
                    int temp_distance = dist(node_X, node_Y, dst_X, dst_Y);

                    if (temp_distance < distance_A)
                    {
                        nextHopAddress = ipSender;
                        nextHopPacket = packet;
                        distance_A = temp_distance;
                        check_A = true;
                    }
                }

                if (canNow_rowIndex == dst_row && canNow_colIndex == dst_col)
                {
                    int temp_distance = dist(node_X, node_Y, dst_X, dst_Y);

                    if (temp_distance < distance_B)
                    {
                        canNextHopAddress = ipSender;
                        canNextHopPacket = packet;
                        distance_B = temp_distance;
                        check_B = true;
                    }
                }
            }
        }
    }

    if (check_A == true)
    {
        InetSocketAddress remote = InetSocketAddress(nextHopAddress, 80);
        socket->Connect(remote);
        currentNode.clearPacketsBuffer();
        Simulator::Schedule(Seconds(interval), &GenerateTraffic, socket, nextHopPacket, UID, ttl);
    }
    else if (check_B == true)
    {
        InetSocketAddress remote = InetSocketAddress(canNextHopAddress, 80);
        socket->Connect(remote);
        currentNode.clearPacketsBuffer();
        Simulator::Schedule(Seconds(interval), &GenerateTraffic, socket, canNextHopPacket, UID, ttl);
    }
    else
    {
        Simulator::Schedule(Seconds(interval), &ScheduleNeighbor, currentNode, socket, destinationId);
    }
}

void ReceivePacket(Ptr<Socket> socket)
{
    Address from;
    Ipv4Address ipSender, ipReceiver, destinationAddress;
    Ptr<Packet> pkt;
    int originalPayloadType;
    uint32_t destinationId;

    Ptr<Ipv4> ipv4 = socket->GetNode()->GetObject<Ipv4>();
    Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
    ipReceiver = iaddr.GetLocal();
    NodeHandler currentNode = nodeHandlerArray[socket->GetNode()->GetId()];

    while (pkt = socket->RecvFrom(from))
    {
        currentNode.increaseBytesReceived();
        currentNode.increasePacketsReceived(1);
        currentNode.increaseBuffer();

        ipSender = InetSocketAddress::ConvertFrom(from).GetIpv4();
        double time = Simulator::Now().GetSeconds();

        PayLoadConstructor payload = PayLoadConstructor(HELLO);
        payload.fromPacket(pkt);

        uint32_t UID = payload.getUid();
        uint32_t ttl = payload.getTtl();
        destinationId = payload.getDestinationId();
        destinationAddress = payload.getDestinationAddress();
        originalPayloadType = payload.getType();

        NS_LOG_UNCOND(time << "s\t" << ipReceiver << "    " << socket->GetNode()->GetId() << "    Received pkt type: " << originalPayloadType << "    with uid    " << UID << "    from: " << ipSender << " to: " << destinationAddress);

        // neighbor nodes receive the packet
        if (payload.getType() == HELLO)
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

            if ((dataForPackets[UID].start + (double)ttl >= Simulator::Now().GetSeconds()) && exist == true)
            {
                payload.setType(HELLO_ACK);

                Ptr<MobilityModel> mob = socket->GetNode()->GetObject<MobilityModel>();
                payload.setNeighbor(ipReceiver);
                payload.setX(mob->GetPosition().x);
                payload.setY(mob->GetPosition().y);
                Ptr<Packet> packet = payload.toPacket();

                InetSocketAddress remote = InetSocketAddress(ipSender, 80);
                socket->SetAllowBroadcast(true);
                socket->Connect(remote);

                Simulator::Schedule(Seconds(interval), &GenerateTraffic, socket, packet, UID, ttl);
            }
        }

        // host node receive neighbor nodes' information
        else if (payload.getType() == HELLO_ACK)
        {
            payload.setType(HELLO_ACK2);
            currentNode.savePacketsInBuffer(payload);
        }

        // selected neighbor node receive the packet
        else if (payload.getType() == HELLO_ACK2)
        {
            if ((payload.getDestinationAddress() != ipReceiver))
            {
                payload.setType(HELLO);
                Ptr<Packet> packet = payload.toPacket();

                InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
                socket->SetAllowBroadcast(true);
                socket->Connect(remote);
                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << ipReceiver << " trans-send the package with uid: " << payload.getUid());

                Simulator::Schedule(Seconds(interval), &GenerateTraffic, socket, packet, UID, ttl);
            }
            else
            {
                if (dataForPackets[payload.getUid()].delivered != true)
                {
                    dataForPackets[payload.getUid()].delivered = true;
                    dataForPackets[payload.getUid()].delivered_at = Simulator::Now().GetSeconds();
                    dataForPackets[payload.getUid()].ttl = ttl;

                    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t PKT DESTINATION REACHED, UID:    " << payload.getUid());
                }
                else
                {
                    NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s I am " << ipReceiver << " RE-received the package with uid: " << UID);
                }
            }
        }
    }

    // host node selects one neighbor node to send the packet
    if (originalPayloadType == HELLO_ACK)
    {
        Simulator::Schedule(Seconds(interval), &ScheduleNeighbor, currentNode, socket, destinationId);
    }
}

int main(int argc, char *argv[])
{
    std::string phyMode("DsssRate11Mbps");
    double distance = 500;
    interval = 0.1;

    // double simulationTime = 569.00;
    double simulationTime = 60.00;
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

        for (uint32_t i = 0; i < numPair * 2; ++i)
        {
            tokens.push_back(std::to_string(i));
        }

        // for (std::vector<std::string>::iterator iter = tokens.begin(); iter != tokens.end(); iter++)
        //     std::cout << *iter << " ";
        // std::cout << std::endl;

        existNode.push_back(tokens);
    }
    file.close();

    file.open("/home/ycpin/平日_7-9.txt", std::ios::in);
    uint32_t count_a = 0, count_b = 0;

    while (getline(file, tempstr))
    {
        std::stringstream ss(tempstr);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string> tokens(begin, end);

        for (int i = 0; i < 4; i++)
            nextHopGrid[count_a][count_b][i] = tokens[i];

        count_b += 1;

        if (count_b == BOARD_ROWS * BOARD_COLS)
        {
            count_b = 0;
            count_a += 1;
        }

        std::cout << count_a << " " << count_b << std::endl;
    }
    file.close();

    SeedManager::SetSeed(seed);

    // The below set of helpers will help us to put together the wifi NICs we want
    WifiHelper wifi;
    YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();

    wifiPhy.Set("RxGain", DoubleValue(4));
    wifiPhy.Set("TxGain", DoubleValue(4));
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(distance)); // set to 250m
    wifiPhy.SetChannel(wifiChannel.Create());

    WifiMacHelper wifiMac;
    wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue(phyMode),
                                 "ControlMode", StringValue(phyMode));
    // Set it to adhoc mode
    wifiMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, c);

    // Import the trace file.
    Ns2MobilityHelper ns2 = Ns2MobilityHelper("/home/ycpin/mobility_test.tcl");
    ns2.Install(); // configure movements for each node, while reading trace file

    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;
    NS_LOG_INFO("Assign IP Addresses.");
    ipv4.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer container = ipv4.Assign(devices);

    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);

    Ptr<Socket> recvSinkArray[numNodes];
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        nodeHandlerArray.push_back(*new NodeHandler(c.Get(i)->GetId()));
        recvSinkArray[i] = Socket::CreateSocket(c.Get(i), tid);
        recvSinkArray[i]->Bind(local);
        recvSinkArray[i]->SetRecvCallback(MakeCallback(&ReceivePacket));
    }

    for (double t = 0; t < simulationTime - sendUntil; t += sendAfter)
    {
        for (uint32_t i = 0; i < numPair; i++)
        {
            sinkNode = i * 2;       // destination Id
            sourceNode = i * 2 + 1; // source Id

            // destination node
            Ipv4InterfaceAddress iaddr = c.Get(sinkNode)->GetObject<Ipv4>()->GetAddress(1, 0);
            Ipv4Address ipReceiver = iaddr.GetLocal();

            // source node
            // Ipv4InterfaceAddress iaddrSender = c.Get(sourceNode)->GetObject<Ipv4>()->GetAddress(1, 0);
            // Ipv4Address ipSender = iaddrSender.GetLocal();

            // broadcast
            Ptr<Socket> source = Socket::CreateSocket(c.Get(sourceNode), tid);
            InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
            source->SetAllowBroadcast(true);
            source->Connect(remote);

            PayLoadConstructor payload = PayLoadConstructor(HELLO);
            payload.setTtl(TTL);
            payload.setUid(UID);
            payload.setDestinationAddress(ipReceiver);
            payload.setDestinationId(sinkNode);
            Ptr<Packet> packet = payload.toPacket();

            PacketLogData dataPacket = {false, -1, 0.00, 0};
            dataForPackets.push_back(dataPacket);

            // Simulator::Schedule(Seconds(t), &GenerateTraffic, source, packet, UID, createStringAddressUid(ipSender, (int)UID, ";"), TTL);
            Simulator::Schedule(Seconds(t), &GenerateTraffic, source, packet, UID, TTL);
            UID += 1;
        }
    }

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
            NS_LOG_UNCOND("- Packets " << i + 1 << " End-to-End Delay: \t" << 0);
        }
    }
    if (debugLevel != "NONE")
    {
        NS_LOG_UNCOND("- Packets sent: \t" << (int)dataForPackets.size());
        NS_LOG_UNCOND("- Packets delivered: \t" << deliveredCounter);
        NS_LOG_UNCOND("- Delivery percentage: \t" << ((double)deliveredCounter / (double)dataForPackets.size()) * 100.00 << "%");
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
