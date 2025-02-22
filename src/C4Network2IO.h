/*
 * LegacyClonk
 *
 * Copyright (c) RedWolf Design
 * Copyright (c) 2013-2018, The OpenClonk Team and contributors
 * Copyright (c) 2017-2021, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

#pragma once

#include "C4Network2Address.h"
#include "C4NetIO.h"
#include "C4Client.h"
#include "C4InteractiveThread.h"
#include "C4Log.h"
#include "C4PuncherPacket.h"

#include <atomic>
#include <cstdint>

class C4Network2IOConnection;

// constants

const int C4NetTimer = 500, // ms
          C4NetPingFreq = 1000, // ms
          C4NetStatisticsFreq = 1000, // ms
          C4NetAcceptTimeout = 10, // s
          C4NetPingTimeout = 30000; // ms

// client count
const int C4NetMaxClients = 256;

class C4Network2IO
	: protected C4InteractiveThread::Callback,
	protected C4NetIO::CBClass,
	protected StdSchedulerProc
{
public:
	C4Network2IO();
	virtual ~C4Network2IO();

protected:
	// main traffic net i/o classes
	C4NetIO *pNetIO_TCP, *pNetIO_UDP;

	// discovery net i/o
	class C4Network2IODiscover *pNetIODiscover;

	// UPnP
	std::unique_ptr<class C4Network2UPnP> UPnP;

	// reference server
	class C4Network2RefServer *pRefServer;

	// local client core
	C4ClientCore LCCore;
	CStdCSec LCCoreCSec;

	// connection list
	C4Network2IOConnection *pConnList;
	CStdCSec ConnListCSec, BroadcastCSec;

	// next connection ID to use
	uint32_t iNextConnID;

	// allow incoming connections?
	bool fAllowConnect;

	// connection acceptance
	struct AutoAccept
	{
		C4ClientCore CCore;
		AutoAccept *Next;
	}
	*pAutoAcceptList;
	CStdCSec AutoAcceptCSec;

	// make sure only one connection is established?
	bool fExclusiveConn;

	// timer & ping
	unsigned long iLastExecute, iLastPing;

	// statistics
	unsigned long iLastStatistic;
	int iTCPIRate, iTCPORate, iTCPBCRate,
		iUDPIRate, iUDPORate, iUDPBCRate;

	// punching
	C4NetIO::addr_t PuncherAddrIPv4, PuncherAddrIPv6;
	bool IsPuncherAddr(const C4NetIO::addr_t &addr) const;

	// logger
	std::shared_ptr<spdlog::logger> logger;

public:
	bool hasTCP() const { return !!pNetIO_TCP; }
	bool hasUDP() const { return !!pNetIO_UDP; }

	// initialization
	bool Init(std::uint16_t iPortTCP, std::uint16_t iPortUDP, std::uint16_t iPortDiscovery = 0, std::uint16_t iPortRefServer = 0); // by main thread
	void Clear(); // by main thread
	void SetLocalCCore(const C4ClientCore &CCore); // by main thread

	// i/o types
	C4NetIO *MsgIO(); // by both
	C4NetIO *DataIO(); // by both

	// connections
	bool Connect(const C4NetIO::addr_t &addr, C4Network2IOProtocol prot, const C4ClientCore &ccore, const char *password = nullptr); // by main thread
	bool ConnectWithSocket(const C4NetIO::addr_t &addr, C4Network2IOProtocol eProt, const C4ClientCore &nCCore, std::unique_ptr<C4NetIOTCP::Socket> socket, const char *szPassword = nullptr); // by main thread
	void SetAcceptMode(bool fAcceptAll); // by main thread
	void SetExclusiveConnMode(bool fExclusiveConn); // by main thread
	int getConnectionCount(); // by main thread

	void ClearAutoAccept(); // by main thread
	void AddAutoAccept(const C4ClientCore &CCore); // by main thread
	void RemoveAutoAccept(const C4ClientCore &CCore); // by main thread

	C4Network2IOConnection *GetMsgConnection(int iClientID); // by both (returns referenced connection!)
	C4Network2IOConnection *GetDataConnection(int iClientID); // by both (returns referenced connection!)

	// broadcasting
	void BeginBroadcast(bool fSelectAll = false); // by both
	void EndBroadcast(); // by both
	bool Broadcast(const C4NetIOPacket &rPkt); // by both

	// sending helpers
	bool BroadcastMsg(const C4NetIOPacket &rPkt); // by both

	// punch
	bool InitPuncher(C4NetIO::addr_t puncherAddr); // by main thread
	void SendPuncherPacket(const C4NetpuncherPacket &, C4Network2HostAddress::AddressFamily family);
	void Punch(const C4NetIO::addr_t &); // Sends a ping packet

	// stuff
	C4NetIO *getNetIO(C4Network2IOProtocol eProt); // by both
	const char *getNetIOName(C4NetIO *pNetIO);
	C4Network2IOProtocol getNetIOProt(C4NetIO *pNetIO);

	// statistics
	int getProtIRate (C4Network2IOProtocol eProt) const { return eProt == P_TCP ? iTCPIRate  : iUDPIRate; }
	int getProtORate (C4Network2IOProtocol eProt) const { return eProt == P_TCP ? iTCPORate  : iUDPORate; }
	int getProtBCRate(C4Network2IOProtocol eProt) const { return eProt == P_TCP ? iTCPBCRate : iUDPBCRate; }

	// reference
	void SetReference(class C4Network2Reference *pReference);
	bool IsReferenceNeeded();

protected:
	// *** callbacks
	// C4NetIO-Callbacks
	virtual bool OnConn(const C4NetIO::addr_t &addr, const C4NetIO::addr_t &AddrConnect, const C4NetIO::addr_t *pOwnAddr, C4NetIO *pNetIO) override;
	virtual void OnDisconn(const C4NetIO::addr_t &addr, C4NetIO *pNetIO, const char *szReason) override;
	virtual void OnPacket(const C4NetIOPacket &rPacket, C4NetIO *pNetIO) override;
	// StdSchedulerProc
	virtual bool Execute(int iTimeout) override;
	virtual int GetTimeout() override;
	// Event callback by C4InteractiveThread
	void OnThreadEvent(C4InteractiveEventType eEvent, const std::any &eventData) override; // by main thread

	// connections list
	void AddConnection(C4Network2IOConnection *pConn); // by both
	void RemoveConnection(C4Network2IOConnection *pConn); // by both
	C4Network2IOConnection *GetConnection(const C4NetIO::addr_t &addr, C4NetIO *pNetIO); // by both
	C4Network2IOConnection *GetConnectionByConnAddr(const C4NetIO::addr_t &addr, C4NetIO *pNetIO); // by both
	C4Network2IOConnection *GetConnectionByID(uint32_t iConnID); // by thread

	// network events (signals to main thread)
	struct NetEvPacketData;

	// connection acceptance
	bool doAutoAccept(const C4ClientCore &CCore, const C4Network2IOConnection &Conn);

	// general packet handling (= forward in most cases)
	bool HandlePacket(const C4NetIOPacket &rPacket, C4Network2IOConnection *pConn, bool fThread); // by both
	void CallHandlers(int iHandlers, const class C4IDPacket *pPacket, C4Network2IOConnection *pConn, bool fThread); // by both

	// packet handling (some are really handled here)
	void HandlePacket(char cStatus, const C4PacketBase *pPacket, C4Network2IOConnection *pConn);
	void HandleFwdReq(const class C4PacketFwd &rFwd, C4Network2IOConnection *pBy);
	void HandlePuncherPacket(const C4NetIOPacket &packet);

	// misc
	bool Ping();
	void CheckTimeout();
	void GenerateStatistics(int iInterval);
	void SendConnPackets();
};

C4LOGGERCONFIG_NAME_TYPE(C4Network2IO);

template<>
struct C4LoggerConfig::Defaults<C4Network2IO>
{
	static constexpr spdlog::level::level_enum GuiLogLevel{spdlog::level::err};
};

enum C4Network2IOConnStatus
{
	CS_Connect,      // waiting for connection
	CS_Connected,    // waiting for Conn
	CS_HalfAccepted, // got Conn (peer identified, client class created if neccessary)
	CS_Accepted,     // got ConnRe (peer did accept)
	CS_Closed,
	CS_ConnectFail,  // got closed before HalfAccepted was reached
};

class C4Network2IOConnection // shared
{
	friend class C4Network2IO;

public:
	C4Network2IOConnection();
	~C4Network2IOConnection();

protected:
	// connection data
	class C4NetIO *pNetClass;
	C4Network2IOProtocol eProt;
	C4NetIO::addr_t PeerAddr, ConnectAddr;
	std::unique_ptr<C4NetIOTCP::Socket> tcpSimOpenSocket;

	// status data
	C4Network2IOConnStatus Status;
	uint32_t iID, iRemoteID; // connection ID for this and the remote client
	bool fAutoAccept; // auto accepted by thread?
	bool fBroadcastTarget; // broadcast target?
	time_t iTimestamp; // timestamp of last status change
	int iPingTime; // ping
	unsigned long iLastPing; // if > iLastPong, it's the first ping that hasn't been answered yet
	unsigned long iLastPong; // last pong received
	C4ClientCore CCore; // client core (>= CS_HalfAccepted)
	CStdCSec CCoreCSec;
	int iIRate, iORate; // input/output rates (by C4NetIO, in b/s)
	int iPacketLoss; // lost packets (in the last seconds)
	StdStrBuf Password; // password to use for connect
	bool fConnSent; // initial connection packet send
	bool fPostMortemSent; // post mortem send

	// packet backlog
	uint32_t iOutPacketCounter, iInPacketCounter;
	struct PacketLogEntry
	{
		uint32_t Number;
		C4NetIOPacket Pkt;
		PacketLogEntry *Next;
	};
	PacketLogEntry *pPacketLog;
	CStdCSec PacketLogCSec;

	// list (C4Network2IO)
	C4Network2IOConnection *pNext;

	// reference counter
	std::atomic<long> iRefCnt;

public:
	C4NetIO               *getNetClass()    const { return pNetClass; }
	C4Network2IOProtocol   getProtocol()    const { return eProt; }
	const C4NetIO::addr_t &getPeerAddr()    const { return PeerAddr.GetPort() ? PeerAddr : ConnectAddr; }
	const C4NetIO::addr_t &getConnectAddr() const { return ConnectAddr; }
	uint32_t               getID()          const { return iID; }
	time_t                 getTimestamp()   const { return iTimestamp; }
	const C4ClientCore    &getCCore()       const { return CCore; }
	int                    getClientID()    const { return CCore.getID(); }
	bool                   isHost()         const { return CCore.isHost(); }
	int                    getPingTime()    const { return iPingTime; }
	int                    getLag()         const;
	int                    getPacketLoss()  const { return iPacketLoss; }
	const char            *getPassword()    const { return Password.getData(); }
	bool                   isConnSent()     const { return fConnSent; }

	uint32_t getInPacketCounter()  const { return iInPacketCounter; }
	uint32_t getOutPacketCounter() const { return iOutPacketCounter; }

	bool isConnecting()      const { return Status == CS_Connect; }
	bool isOpen()            const { return Status != CS_Connect && Status != CS_Closed && Status != CS_ConnectFail; }
	bool isHalfAccepted()    const { return Status == CS_HalfAccepted || Status == CS_Accepted; }
	bool isAccepted()        const { return Status == CS_Accepted; }
	bool isClosed()          const { return Status == CS_Closed || Status == CS_ConnectFail; }
	bool isAutoAccepted()    const { return fAutoAccept; }
	bool isBroadcastTarget() const { return fBroadcastTarget; }
	bool isFailed()          const { return Status == CS_ConnectFail; }

protected:
	// called by C4Network2IO only
	void Set(C4NetIO *pnNetClass, C4Network2IOProtocol eProt, const C4NetIO::addr_t &nPeerAddr, const C4NetIO::addr_t &nConnectAddr, C4Network2IOConnStatus nStatus, const char *szPassword, uint32_t iID);
	void SetSocket(std::unique_ptr<C4NetIOTCP::Socket> socket);
	void SetRemoteID(uint32_t iRemoteID);
	void SetPeerAddr(const C4NetIO::addr_t &nPeerAddr);
	void OnPing();
	void SetPingTime(int iPingTime);
	void SetStatus(C4Network2IOConnStatus nStatus);
	void SetAutoAccepted();
	void OnPacketReceived(uint8_t iPacketType);
	void ClearPacketLog(uint32_t iStartNumber = ~0);

public:
	// status changing
	void SetHalfAccepted() { SetStatus(CS_HalfAccepted); }
	void SetAccepted() { SetStatus(CS_Accepted); }
	void SetCCore(const C4ClientCore &nCCore);
	void ResetAutoAccepted() { fAutoAccept = false; }
	void SetConnSent() { fConnSent = true; }

	// connection operations
	bool Connect();
	void Close();
	bool Send(const C4NetIOPacket &rPkt);
	void SetBroadcastTarget(bool fSet); // (only call after C4Network2IO::BeginBroadcast!)

	// statistics
	void DoStatistics(int iInterval, int *pIRateSum, int *pORateSum);

	// reference counting
	void AddRef(); void DelRef();

	// post mortem
	bool CreatePostMortem(class C4PacketPostMortem *pPkt);
};

// Packets

class C4PacketPing : public C4PacketBase
{
public:
	C4PacketPing(uint32_t iPacketCounter = 0);

protected:
	uint32_t iTime;
	uint32_t iPacketCounter;

public:
	uint32_t getTravelTime() const;
	uint32_t getPacketCounter() const { return iPacketCounter; }

	virtual void CompileFunc(StdCompiler *pComp) override;
};

class C4PacketConn : public C4PacketBase
{
public:
	C4PacketConn();
	C4PacketConn(const class C4ClientCore &nCCore, uint32_t iConnID, const char *szPassword = nullptr);

protected:
	int32_t iVer;
	uint32_t iConnID;
	C4ClientCore CCore;
	StdStrBuf Password;

public:
	int32_t getVer()               const { return iVer; }
	uint32_t getConnID()           const { return iConnID; }
	const C4ClientCore &getCCore() const { return CCore; }
	const char *getPassword()      const { return Password.getData(); }

	virtual void CompileFunc(StdCompiler *pComp) override;
};

class C4PacketConnRe : public C4PacketBase
{
public:
	C4PacketConnRe();
	C4PacketConnRe(bool fOK, bool fWrongPassword, const char *szMsg = nullptr);

protected:
	bool fOK, fWrongPassword;
	StdStrBuf szMsg;

public:
	bool isOK() const { return fOK; }
	bool isPasswordWrong() const { return fWrongPassword; }
	const char *getMsg() const { return szMsg.getData(); }

	virtual void CompileFunc(StdCompiler *pComp) override;
};

class C4PacketFwd : public C4PacketBase
{
public:
	C4PacketFwd();

protected:
	bool fNegativeList;
	int32_t iClients[C4NetMaxClients];
	int32_t iClientCnt;
	C4NetIOPacket Data;

public:
	const C4NetIOPacket &getData() const { return Data; }
	int32_t getClient(int32_t i)   const { return iClients[i]; }
	int32_t getClientCnt()         const { return iClientCnt; }

	bool DoFwdTo(int32_t iClient) const;

	void SetData(const C4NetIOPacket &Pkt);
	void SetListType(bool fnNegativeList);
	void AddClient(int32_t iClient);

	virtual void CompileFunc(StdCompiler *pComp) override;
};

class C4PacketPostMortem : public C4PacketBase
{
public:
	C4PacketPostMortem();
	~C4PacketPostMortem();

private:
	uint32_t iConnID;
	uint32_t iPacketCounter; // last packet counter of dead connection
	uint32_t iPacketCount;
	struct PacketLink
	{
		C4NetIOPacket Pkt;
		PacketLink *Next;
	};
	PacketLink *pPackets;

public:
	const uint32_t getConnID() const { return iConnID; }
	const uint32_t getPacketCount() const { return iPacketCount; }
	void SetConnID(uint32_t inConnID) { iConnID = inConnID; }

	const C4NetIOPacket *getPacket(uint32_t iNumber) const;
	void SetPacketCounter(uint32_t iPacketCounter);
	void Add(const C4NetIOPacket &rPkt);

	virtual void CompileFunc(StdCompiler *pComp) override;
};
