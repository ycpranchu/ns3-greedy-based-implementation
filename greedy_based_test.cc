#include <stack>
#include <vector>
#include <algorithm>
#include <fstream>
#include <random>
#include <iostream>
#include <iterator>
#include <sstream>
#include <math.h>
#include <random>

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
#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"

using namespace ns3;

// Define a Log component with a specific name.
NS_LOG_COMPONENT_DEFINE("DQG-GREEDY_BASED");

// Define enumeration for PayLoad type
enum
{
    HELLO,
    STANDARD,
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
TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
NodeContainer c;
double distance;
double interval = 0.1;
uint32_t helloSendAfter = 5;
uint32_t numPair = 150;

uint32_t gridSize = 1000;
uint32_t BOARD_ROWS = 4;
uint32_t BOARD_COLS = 6;
std::string nextHopGrid[24][24][8];

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

std::string createStringAddressUid(Ipv4Address address, int uid, int type, std::string delimiter)
{
    std::ostringstream value;
    value << address << delimiter << uid << delimiter << type;
    return value.str();
}

class PayLoadConstructor
{
private:
    int type;
    uint32_t destinationId;
    uint32_t ttl;
    uint32_t uid;
    uint32_t neighborId;
    Ipv4Address nextHopAddress, neighborAddress;
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
    uint32_t getNeighborId() { return neighborId; };
    Ipv4Address getNeighborAddress() { return neighborAddress; };
    uint32_t getDestinationId() { return destinationId; };
    Ipv4Address getNextHopAddress() { return nextHopAddress; };
    Ipv4Address getDestinationAddress() { return destinationAddress; };

    void setTtl(uint32_t value) { ttl = value; };
    void setUid(uint32_t value) { uid = value; };
    void setType(int value) { type = value; };
    void setNeighborId(uint32_t value) { neighborId = value; };
    void setNeighborAddress(Ipv4Address value) { neighborAddress = value; };
    void setDestinationId(uint32_t value) { destinationId = value; };
    void setNextHopAddress(Ipv4Address value) { nextHopAddress = value; };
    void setDestinationAddress(Ipv4Address value) { destinationAddress = value; };
    void setDestinationAddressFromString(std::string value) { destinationAddress = ns3::Ipv4Address(value.c_str()); };

    void fromString(std::string stringPayload)
    {
        std::vector<std::string> values = splitString(stringPayload, delimiter);

        type = std::stoi(values[0]);
        nextHopAddress = ns3::Ipv4Address(values[1].c_str());
        destinationAddress = ns3::Ipv4Address(values[2].c_str());
        destinationId = std::stoi(values[3]);
        ttl = std::stoi(values[4]);
        uid = std::stoi(values[5]);
        neighborAddress = ns3::Ipv4Address(values[6].c_str());
        neighborId = std::stoi(values[7].c_str());
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
        msg << getType() << delimiter << nextHopAddress << delimiter << destinationAddress << delimiter << destinationId << delimiter << ttl << delimiter << uid << delimiter << neighborAddress << delimiter << neighborId;
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
    std::vector<uint32_t> findNeighbor;
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
        maxBufferSize = 10000000;
        packetSize = 1024;

        for (int i = 0; i < 4000; i++)
        {
            findNeighbor.push_back(0);
        }
    }

    int getNodeID() { return nodeid; }
    double getBytesSent() { return bytesSent; }
    int getPacketsSent() { return packetsSent; }
    double getBytesReceived() { return bytesReceived; }
    int getPacketsReceived() { return packetsReceived; }
    int getFindNeighbor(uint32_t value) { return findNeighbor[value]; }

    void setBytesSent(double value) { bytesSent = value; }
    void setPacketsSent(double value) { packetsSent = value; }
    void setBytesReceived(double value) { bytesReceived = value; }
    void setPacketsReceived(double value) { packetsReceived = value; }
    void setFindNeighbor(uint32_t value) { findNeighbor[value] = helloSendAfter; }

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

    std::string pushInReceived(ns3::Ipv4Address nextHopAddress, int uid, int type)
    {
        std::string value = createStringAddressUid(nextHopAddress, uid, type, ";");
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
    // Ptr<Ipv4> ipv4 = socket->GetNode()->GetObject<Ipv4>();
    // Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
    // Ipv4Address ipSender = iaddr.GetLocal();

    NodeHandler *currentNode = &nodeHandlerArray[socket->GetNode()->GetId()];
    socket->Send(packet);

    if (UID != 0)
        if (dataForPackets[UID].start == -1)
            dataForPackets[UID].start = Simulator::Now().GetSeconds();

    currentNode->increaseBytesSent();
    currentNode->increasePacketsSent(1);
    currentNode->decreaseBuffer();
}

float dist(float x1, float y1, float x2, float y2)
{
    float dist = sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
    return dist;
}

void ScheduleNeighbor(Ptr<Socket> socket, Ptr<Packet> packet, NodeHandler *currentNode, uint32_t destinationId)
{
    bool exist = false;
    double time = Simulator::Now().GetSeconds();

    for (std::vector<std::string>::iterator iter = existNode[(int)time].begin(); iter != existNode[(int)time].end(); iter++)
    {
        if ((int)currentNode->getNodeID() == stoi(*iter))
        {
            exist = true;
        }
    }

    if (exist == false)
        return;

    int index_row[3], index_col[3]; // source, destination, neighbor
    int select_row[4] = {-1, -1, -1, -1};
    int select_col[4] = {-1, -1, -1, -1};

    Ptr<MobilityModel> current_mob = c.Get(currentNode->getNodeID())->GetObject<MobilityModel>();
    float src_X = current_mob->GetPosition().x;
    float src_Y = current_mob->GetPosition().y;
    index_row[0] = src_Y / gridSize;
    index_col[0] = src_X / gridSize;

    Ptr<MobilityModel> dst_mob = c.Get(destinationId)->GetObject<MobilityModel>();
    float dst_X = dst_mob->GetPosition().x;
    float dst_Y = dst_mob->GetPosition().y;
    index_row[1] = dst_Y / gridSize;
    index_col[1] = dst_X / gridSize;

    for (int i = 0; i < 2; i++)
    {
        if (index_row[i] < 0)
            index_row[i] = 0;

        if (index_col[i] < 0)
            index_col[i] = 0;

        if (index_row[i] >= 4)
            index_row[i] = 3;

        if (index_col[i] >= 6)
            index_col[i] = 5;
    }

    bool dst_grid = true;
    for (int i = 0; i < 8; i++)
        if (std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str()) != -1)
            dst_grid = false;

    int first_choice = -1;
    int second_choice = -1;
    int third_choice = -1;

    if (dst_grid == false)
    {
        float max = -1;
        for (int i = 0; i < 8; i++) // choise neighbor grid with max grid value
        {
            if (std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str()) > max)
            {
                first_choice = i;
                max = std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str());
            }
        }

        max = -1;
        for (int i = 0; i < 8; i++) // neighbor grid with sub-max grid value
        {
            if (i == first_choice)
                continue;

            if (std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str()) > max)
            {
                second_choice = i;
                max = std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str());
            }
        }

        max = -1;
        for (int i = 0; i < 8; i++) // neighbor grid with sub-max grid value
        {
            if (i == first_choice || i == second_choice)
                continue;

            if (std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str()) > max)
            {
                third_choice = i;
                max = std::atof(nextHopGrid[index_row[0] * BOARD_COLS + index_col[0]][index_row[1] * BOARD_COLS + index_col[1]][i].c_str());
            }
        }
    }

    if (dst_grid == true)
    {
        select_row[0] = index_row[0];
        select_col[0] = index_col[0];
    }
    else
    {
        for (int i = 0; i < 4; i++)
        {
            int choice;

            if (i == 0)
            {
                choice = first_choice;
            }
            else if (i == 1)
            {
                choice = second_choice;
            }
            else if (i == 2)
            {
                choice = third_choice;
            }
            else if (i == 3)
            {
                select_row[i] = index_row[0];
                select_col[i] = index_col[0];
                continue;
            }

            if (choice == 0)
            {
                select_row[i] = index_row[0] - 1;
                select_col[i] = index_col[0];
            }
            else if (choice == 1)
            {
                select_row[i] = index_row[0] + 1;
                select_col[i] = index_col[0];
            }
            else if (choice == 2)
            {
                select_row[i] = index_row[0];
                select_col[i] = index_col[0] - 1;
            }
            else if (choice == 3)
            {
                select_row[i] = index_row[0];
                select_col[i] = index_col[0] + 1;
            }
            else if (choice == 4)
            {
                select_row[i] = index_row[0] - 1;
                select_col[i] = index_col[0] + 1;
            }
            else if (choice == 5)
            {
                select_row[i] = index_row[0] + 1;
                select_col[i] = index_col[0] - 1;
            }
            else if (choice == 6)
            {
                select_row[i] = index_row[0] - 1;
                select_col[i] = index_col[0] - 1;
            }
            else if (choice == 7)
            {
                select_row[i] = index_row[0] + 1;
                select_col[i] = index_col[0] + 1;
            }
        }
    }

    // std::cout << first_choice << ", " << second_choice << ", " << third_choice << std::endl;
    // for (int i = 0; i < 3; i++)
    //     std::cout << select_row[i] << "," << select_col[i] << "\t";
    // std::cout << std::endl;

    Ipv4Address nextHopAddress[4];
    bool check[4] = {false, false, false, false};
    double rec_distance[4] = {100000, 100000, 100000, 100000};

    PayLoadConstructor payload = PayLoadConstructor(HELLO);
    payload.fromPacket(packet);

    uint32_t UID = payload.getUid();
    uint32_t ttl = payload.getTtl();
    int temp_distance, validation;

    for (std::vector<std::string>::iterator iter = existNode[(int)time].begin(); iter != existNode[(int)time].end(); iter++)
    {
        uint32_t node = stoi(*iter);

        if (currentNode->getFindNeighbor(node) == 0)
            continue;

        Ptr<Ipv4> ipv4 = c.Get(node)->GetObject<Ipv4>();
        Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
        Ipv4Address ipSender = iaddr.GetLocal();

        Ptr<MobilityModel> node_mob = c.Get(node)->GetObject<MobilityModel>();
        NodeHandler *neighborNode = &nodeHandlerArray[node];

        float node_X = node_mob->GetPosition().x;
        float node_Y = node_mob->GetPosition().y;
        index_row[2] = node_Y / gridSize;
        index_col[2] = node_X / gridSize;

        if ((index_row[2] < 0 || index_row[2] > 3) || (index_col[2] < 0 || index_col[2] > 5))
            continue;

        validation = dist(node_X, node_Y, src_X, src_Y);

        if (payload.getDestinationAddress() == ipSender && validation <= distance)
        {
            nextHopAddress[0] = ipSender;
            check[0] = true;
            break;
        }

        if (node < numPair * 2)
            continue;

        for (int i = 0; i < 4; i++)
        {
            if (select_row[i] == index_row[2] && select_col[i] == index_col[2])
            {
                // if (i == 0)
                temp_distance = dist(node_X, node_Y, dst_X, dst_Y);
                // else
                //     temp_distance = dist(node_X, node_Y, (select_col[0] - 1) * 1000 + 500, (select_row[0] - 1) * 1000 + 500);

                if (temp_distance < rec_distance[i] && validation <= distance && neighborNode->searchInStack(UID) == false)
                {
                    nextHopAddress[i] = ipSender;
                    rec_distance[i] = temp_distance;
                    check[i] = true;

                    // if ((int)socket->GetNode()->GetId() == 100)
                    // {
                    //     std::cout << "ipSender : " << ipSender << "\t" << node << "\tdistance :" << temp_distance << "\tvalidation :" << validation << "\tgrid : ";
                    //     for (int i = 0; i < 3; i++)
                    //         std::cout << index_row[i] << "," << index_col[i] << "\t";
                    //     std::cout << std::endl;
                    // }
                }
            }
        }
    }

    bool sent = false;
    for (int i = 0; i < 4; i++)
    {
        if (check[i] == true)
        {
            payload.setType(STANDARD);
            payload.setNextHopAddress(nextHopAddress[i]);
            packet = payload.toPacket();

            if (currentNode->searchInStack(UID) == false)
                currentNode->pushInStack(UID);

            std::cout << Simulator::Now().GetSeconds() << "\t" << currentNode->getNodeID() << "\tget nextHopAddress: " << nextHopAddress[i] << std::endl;

            Ptr<Socket> new_socket = Socket::CreateSocket(c.Get(socket->GetNode()->GetId()), tid);
            InetSocketAddress remote = InetSocketAddress(nextHopAddress[i], 80);
            new_socket->Connect(remote);

            Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
            // double randomPause = x->GetValue(0, 0.5);
            Simulator::Schedule(Seconds(0), &GenerateTraffic, new_socket, packet, UID, ttl);
            sent = true;
            break;
        }
    }

    if (sent == false)
    {
        Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
        double randomPause = x->GetValue(0.5, 1);
        Simulator::Schedule(Seconds(randomPause), &ScheduleNeighbor, socket, packet, currentNode, destinationId);
    }
}

void ReceivePacket(Ptr<Socket> socket)
{
    Address from;
    Ipv4Address ipSender;
    Ptr<Packet> packet;
    Ipv4Address destinationAddress;
    uint32_t destinationId;

    Ptr<Ipv4> ipv4 = socket->GetNode()->GetObject<Ipv4>();
    Ipv4InterfaceAddress iaddr = ipv4->GetAddress(1, 0);
    Ipv4Address ipReceiver = iaddr.GetLocal();

    while (packet = socket->RecvFrom(from))
    {
        NodeHandler *currentNode = &nodeHandlerArray[socket->GetNode()->GetId()];
        Ipv4Address ipSender = InetSocketAddress::ConvertFrom(from).GetIpv4();

        currentNode->increasePacketsReceived(1);
        PayLoadConstructor payload = PayLoadConstructor(HELLO);
        payload.fromPacket(packet);

        Ipv4Address nextHopAddress = payload.getNextHopAddress();
        uint32_t neighborId = payload.getNeighborId();
        uint32_t UID = payload.getUid();
        uint32_t TTL = payload.getTtl();

        double time = Simulator::Now().GetSeconds();

        if ((payload.getDestinationAddress() == ipReceiver) && payload.getUid() != 0)
        {
            if (dataForPackets[payload.getUid()].delivered != true)
            {
                dataForPackets[payload.getUid()].delivered = true;
                dataForPackets[payload.getUid()].delivered_at = Simulator::Now().GetSeconds();
                dataForPackets[payload.getUid()].ttl = payload.getTtl();

                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t PKT DESTINATION REACHED, UID: " << payload.getUid());
            }
            else
            {
                NS_LOG_UNCOND(Simulator::Now().GetSeconds() << "s\t " << ipReceiver << "\tRE-received the package with uid: " << UID);
            }

            continue;
        }

        // receive the packet
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

            if (ipSender == nextHopAddress && exist == true)
            {
                currentNode->increaseBytesReceived();
                currentNode->setFindNeighbor(neighborId);
            }
        }

        // selected neighbor node receive the packet
        else if (payload.getType() == STANDARD)
        {
            if (ipReceiver == nextHopAddress && currentNode->searchInStack(UID) == false)
            {
                currentNode->increaseBytesReceived();
                currentNode->increaseBuffer();

                // if ((int)socket->GetNode()->GetId() == 100)
                // NS_LOG_UNCOND(time << "s\t" << ipReceiver << "\t" << socket->GetNode()->GetId() << "\tReceived pkt type: " << payload.getType() << "\twith uid: " << UID << "\tfrom: " << ipSender);

                if ((dataForPackets[UID].start + (double)TTL >= Simulator::Now().GetSeconds()) && currentNode->checkBufferSize())
                {
                    destinationId = payload.getDestinationId();
                    destinationAddress = payload.getDestinationAddress();
                    currentNode->savePacketsInBuffer(payload);

                    Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
                    // double randomPause = x->GetValue(0, 0.5);
                    Simulator::Schedule(Seconds(0), &ScheduleNeighbor, socket, packet, currentNode, destinationId);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    std::string phyMode("DsssRate11Mbps");
    distance = 500;
    helloSendAfter = 1;

    // double simulationTime = 569.00;
    double simulationTime = 100.00;
    double sendUntil = 50.00;
    double warmingTime = 10.00;
    uint32_t seed = 91;

    numPair = 100;
    uint32_t numNodes = 3000;
    uint32_t sendAfter = 5;
    uint32_t sinkNode;
    uint32_t sourceNode;

    uint32_t TTL = 50;
    uint32_t UID = 1;

    CommandLine cmd;
    cmd.AddValue("phyMode", "Wifi Phy mode", phyMode);
    cmd.AddValue("distance", "distance (m)", distance);
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

    file.open("/home/ycpin/Dataset/平日_7_9/exist_file/exist_2022-01-04_100_0.txt", std::ios::in);
    while (getline(file, tempstr))
    {
        std::stringstream ss(tempstr);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string> tokens(begin, end);

        for (unsigned int i = 0; i < tokens.size(); ++i)
        {
            tokens[i] = std::to_string(std::stoi(tokens[i].c_str()) + numPair * 2);
        }

        for (uint32_t i = 0; i < numPair * 2; ++i)
        {
            tokens.push_back(std::to_string(i));
        }

        existNode.push_back(tokens);
    }
    file.close();

    file.open("/home/ycpin/Dataset/Q-table/平日_7-9_test.txt", std::ios::in);
    uint32_t count_a = 0, count_b = 0;

    while (getline(file, tempstr))
    {
        std::stringstream ss(tempstr);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string> tokens(begin, end);

        for (int i = 0; i < 8; i++)
            nextHopGrid[count_a][count_b][i] = tokens[i];

        count_b += 1;

        if (count_b == BOARD_ROWS * BOARD_COLS)
        {
            count_b = 0;
            count_a += 1;
        }
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
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(distance));
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
    Ns2MobilityHelper ns2 = Ns2MobilityHelper("/home/ycpin/Dataset/平日_7_9/mobility_file/mobility_2022-01-04_100_0_test.tcl");
    ns2.Install(); // configure movements for each node, while reading trace file

    InternetStackHelper internet;
    internet.Install(c);

    Ipv4AddressHelper ipv4;
    NS_LOG_INFO("Assign IP Addresses.");
    ipv4.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer container = ipv4.Assign(devices);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);

    Ptr<Socket> recvSinkArray[numNodes];
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        nodeHandlerArray.push_back(*new NodeHandler(c.Get(i)->GetId()));
        recvSinkArray[i] = Socket::CreateSocket(c.Get(i), tid);
        recvSinkArray[i]->Bind(local);
        recvSinkArray[i]->SetRecvCallback(MakeCallback(&ReceivePacket));
    }

    PacketLogData dataPacket = {false, -1, 0.00, 0};
    dataForPackets.push_back(dataPacket); // for packet UID 0

    for (double t = 0; t < simulationTime; t += helloSendAfter)
    {
        for (std::vector<std::string>::iterator iter = existNode[(int)t].begin(); iter != existNode[(int)t].end(); iter++)
        {
            Ipv4InterfaceAddress iaddrSender = c.Get(stoi(*iter))->GetObject<Ipv4>()->GetAddress(1, 0);
            Ipv4Address ipSender = iaddrSender.GetLocal();

            Ptr<Socket> socket = Socket::CreateSocket(c.Get(stoi(*iter)), tid);

            PayLoadConstructor payload = PayLoadConstructor(HELLO);
            payload.setTtl(TTL);
            payload.setUid(0);
            payload.setNextHopAddress(ipSender);
            payload.setNeighborId(stoi(*iter));
            payload.setDestinationAddress(ipSender);
            payload.setDestinationId(stoi(*iter));
            Ptr<Packet> packet = payload.toPacket();

            InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
            socket->Connect(remote);
            socket->SetAllowBroadcast(true);

            Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
            double randomPause = x->GetValue(0, 1);

            Simulator::Schedule(Seconds(t + randomPause), &GenerateTraffic, socket, packet, payload.getUid(), TTL);
        }
    }

    for (double t = warmingTime; t < simulationTime - sendUntil; t += sendAfter)
    {
        for (uint32_t i = 0; i < numPair; i++)
        {
            sourceNode = i * 2;   // source Id
            sinkNode = i * 2 + 1; // destination Id

            // source node
            Ipv4InterfaceAddress iaddrSender = c.Get(sourceNode)->GetObject<Ipv4>()->GetAddress(1, 0);
            Ipv4Address ipSender = iaddrSender.GetLocal();

            // destination node
            Ipv4InterfaceAddress iaddr = c.Get(sinkNode)->GetObject<Ipv4>()->GetAddress(1, 0);
            Ipv4Address ipReceiver = iaddr.GetLocal();

            // Create socket
            Ptr<Socket> socket = Socket::CreateSocket(c.Get(sourceNode), tid);
            NodeHandler *currentNode = &nodeHandlerArray[socket->GetNode()->GetId()];

            PayLoadConstructor payload = PayLoadConstructor(STANDARD);
            payload.setTtl(TTL);
            payload.setUid(UID);
            payload.setNextHopAddress(ipSender);
            payload.setNeighborId(sourceNode);
            payload.setDestinationAddress(ipReceiver);
            payload.setDestinationId(sinkNode);
            Ptr<Packet> packet = payload.toPacket();

            PacketLogData dataPacket = {false, -1, 0.00, 0};
            dataForPackets.push_back(dataPacket);

            // InetSocketAddress remote = InetSocketAddress(Ipv4Address("255.255.255.255"), 80);
            // socket->Connect(remote);
            // socket->SetAllowBroadcast(true);

            Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
            double randomPause = x->GetValue(0, 0.5);

            Simulator::Schedule(Seconds(t + randomPause), &ScheduleNeighbor, socket, packet, currentNode, sinkNode);
            // Simulator::Schedule(Seconds(t + randomPause), &GenerateTraffic, socket, packet, UID, TTL);

            UID += 1;
        }
    }

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    Simulator::Destroy();

    // simulator is ending

    int deliveredCounter = 0;
    double end2endDelay = 0.0;

    for (int i = 1; i < (int)dataForPackets.size(); i++)
    {
        if (dataForPackets[i].delivered == true)
        {
            deliveredCounter++;
            end2endDelay += (double)(dataForPackets[i].delivered_at - dataForPackets[i].start);

            if (debugLevel != "NONE")
            {
                NS_LOG_UNCOND("- Packets " << i << " delta delivery: \t" << (double)(dataForPackets[i].delivered_at - dataForPackets[i].start));
                NS_LOG_UNCOND("- Packets " << i << " End-to-End Delay: \t" << (double)(dataForPackets[i].delivered_at - dataForPackets[i].start));
            }
        }
        else if (debugLevel != "NONE")
        {
            NS_LOG_UNCOND("- Packets " << i << " delta delivery: \t" << 0);
            NS_LOG_UNCOND("- Packets " << i << " End-to-End Delay: \t" << 0);
        }
    }
    if (debugLevel != "NONE")
    {
        NS_LOG_UNCOND("- Packets sent: \t" << (int)dataForPackets.size() - 1);
        NS_LOG_UNCOND("- Packets delivered: \t" << deliveredCounter);
        NS_LOG_UNCOND("- Delivery percentage: \t" << ((double)deliveredCounter / ((double)dataForPackets.size() - 1)) * 100.00 << "%");
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