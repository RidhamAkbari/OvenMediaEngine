//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "ice_port_observer.h"
#include "ice_tcp_demultiplexer.h"
#include "modules/ice/stun/stun_message.h"

#include <vector>
#include <memory>

#include <config/config.h>
#include <modules/rtp_rtcp/rtp_packet.h>
#include <modules/rtp_rtcp/rtcp_packet.h>
#include <modules/physical_port/physical_port_manager.h>

class RtcIceCandidate;

class IcePort : protected PhysicalPortObserver
{
protected:
	// A data structure to tracking client connection status
	struct IcePortInfo
	{
		// Session information that connected with the client
		std::shared_ptr<info::Session> session_info;

		std::shared_ptr<const SessionDescription> offer_sdp;
		std::shared_ptr<const SessionDescription> peer_sdp;

		std::shared_ptr<ov::Socket> remote;
		ov::SocketAddress address;

		IcePortConnectionState state;

		std::chrono::time_point<std::chrono::system_clock> expire_time;

		IcePortInfo(int expire_after_ms)
			: _expire_after_ms(expire_after_ms)
		{
		}

		void UpdateBindingTime()
		{
			expire_time = std::chrono::system_clock::now() + std::chrono::milliseconds(_expire_after_ms);
		}

		bool IsExpired() const
		{
			return (std::chrono::system_clock::now() > expire_time);
		}

	protected:
		const int _expire_after_ms;
	};

	// Additional information analyzed during packet parsing
	struct PacketInfo
	{
		IcePacketIdentifier::PacketType packet_type;

		// If this packet is from a turn data channel, store the channel number.
		uint16_t channel_number;
	};

public:
	IcePort();
	~IcePort() override;

	bool CreateTurnServer(ov::SocketAddress address, ov::SocketType socket_type);
	bool CreateIceCandidates(std::vector<RtcIceCandidate> ice_candidate_list);

	const std::vector<RtcIceCandidate> &GetIceCandidateList() const;

	bool Close();

	IcePortConnectionState GetState(const std::shared_ptr<info::Session> &session_info) const
	{
		OV_ASSERT2(session_info != nullptr);

		auto item = _session_table.find(session_info->GetId());

		if(item == _session_table.end())
		{
			OV_ASSERT(false, "Invalid session_id: %d", session_info->GetId());
			return IcePortConnectionState::Failed;
		}

		return item->second->state;
	}

	ov::String GenerateUfrag();

	bool AddObserver(std::shared_ptr<IcePortObserver> observer);
	bool RemoveObserver(std::shared_ptr<IcePortObserver> observer);
	bool RemoveObservers();

	bool HasObserver() const noexcept
	{
		return (_observers.empty() == false);
	}

	void AddSession(const std::shared_ptr<info::Session> &session_info, std::shared_ptr<const SessionDescription> offer_sdp, std::shared_ptr<const SessionDescription> peer_sdp);
	bool RemoveSession(session_id_t session_id);
	bool RemoveSession(const std::shared_ptr<info::Session> &session_info);

	bool Send(const std::shared_ptr<info::Session> &session_info, std::unique_ptr<RtpPacket> packet);
	bool Send(const std::shared_ptr<info::Session> &session_info, std::unique_ptr<RtcpPacket> packet);
	bool Send(const std::shared_ptr<info::Session> &session_info, const std::shared_ptr<const ov::Data> &data);

	ov::String ToString() const;

protected:
	std::shared_ptr<PhysicalPort> CreatePhysicalPort(const ov::SocketAddress &address, ov::SocketType type);
	bool ParseIceCandidate(const ov::String &ice_candidate, std::vector<ov::String> *ip_list, ov::SocketType *socket_type, int *start_port, int *end_port);

	//--------------------------------------------------------------------
	// Implementation of PhysicalPortObserver
	//--------------------------------------------------------------------
	void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
	void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;
	void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override;
	//--------------------------------------------------------------------

	void SetIceState(std::shared_ptr<IcePortInfo> &info, IcePortConnectionState state);
	void ResponseError(const std::shared_ptr<ov::Socket> &remote);

private:
	void CheckTimedoutItem();

	void ProcessPacket(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, 
						const PacketInfo &packet_info, const std::shared_ptr<const ov::Data> &data);
	void ProcessStunPacket(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, 
						const PacketInfo &packet_info, const std::shared_ptr<const ov::Data> &data);
	void ProcessChannelDataPacket(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, 
						const PacketInfo &packet_info, const std::shared_ptr<const ov::Data> &data);
	void ProcessApplicationPacket(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, 
						const PacketInfo &packet_info, const std::shared_ptr<const ov::Data> &data);

	// STUN negotiation order:
	// (State: New)
	// [Server] <-- 1. Binding Request          --- [Player]
	// (State: Checking)
	// [Server] --- 2. Binding Success Response --> [Player]
	// [Server] --- 3. Binding Request          --> [Player]
	// [Server] <-- 4. Binding Success Response --- [Player]
	// (State: Connected)
	bool ProcessBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const StunMessage &request_message);
	bool SendBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const StunMessage &request_message, const std::shared_ptr<IcePortInfo> &info);
	bool SendBindingRequest(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<IcePortInfo> &info);
	bool ProcessBindingResponse(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const StunMessage &response_message);

	std::vector<std::shared_ptr<PhysicalPort>> _physical_port_list;
	std::recursive_mutex _physical_port_list_mutex;

	// List of observers who will receive events when data comes in from IcePort
	std::vector<std::shared_ptr<IcePortObserver>> _observers;

	std::vector<RtcIceCandidate> _ice_candidate_list;

	// Mapping table containing related information until STUN binding.
	// Once binding is complete, there is no need because it can be found by destination ip & port.
	// key: offer ufrag
	// value: IcePortInfo
	std::map<const ov::String, std::shared_ptr<IcePortInfo>> _user_mapping_table;
	std::mutex _user_mapping_table_mutex;

	// Find IcePortInfo with peer's ip:port
	// key: SocketAddress
	// value: IcePortInfo
	std::mutex _ice_port_info_mutex;
	std::map<ov::SocketAddress, std::shared_ptr<IcePortInfo>> _ice_port_info;
	// Find IcePortInfo with peer's session id
	std::map<session_id_t, std::shared_ptr<IcePortInfo>> _session_table;

	// Demultiplexer for data input through TCP
	// remote's ID : Demultiplexer
	std::shared_mutex _demultiplexers_lock;
	std::map<int, std::shared_ptr<IceTcpDemultiplexer>>	_demultiplexers;

	ov::DelayQueue _timer;
};
