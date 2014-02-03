/*
 * ZeroTier One - Global Peer to Peer Ethernet
 * Copyright (C) 2012-2013  ZeroTier Networks LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * ZeroTier may be used and distributed under the terms of the GPLv3, which
 * are available at: http://www.gnu.org/licenses/gpl-3.0.html
 *
 * If you would like to embed ZeroTier into a commercial application or
 * redistribute it in a modified binary form, please contact ZeroTier Networks
 * LLC. Start here: http://www.zerotier.com/
 */

#ifndef ZT_PEER_HPP
#define ZT_PEER_HPP

#include <stdint.h>

#include <algorithm>
#include <utility>
#include <stdexcept>

#include "Constants.hpp"
#include "Address.hpp"
#include "Utils.hpp"
#include "Identity.hpp"
#include "Logger.hpp"
#include "Demarc.hpp"
#include "RuntimeEnvironment.hpp"
#include "InetAddress.hpp"
#include "Packet.hpp"
#include "SharedPtr.hpp"
#include "AtomicCounter.hpp"
#include "NonCopyable.hpp"
#include "Mutex.hpp"

// Increment if serialization has changed
#define ZT_PEER_SERIALIZATION_VERSION 6

namespace ZeroTier {

/**
 * A peer on the network
 * 
 * Threading note:
 *
 * This structure contains no locks at the moment, but also performs no
 * memory allocation or pointer manipulation. As a result is is technically
 * "safe" for threads, as in won't crash. Right now it's only changed from
 * the core I/O thread so this isn't an issue. If multiple I/O threads are
 * introduced it ought to have a lock of some kind.
 */
class Peer : NonCopyable
{
	friend class SharedPtr<Peer>;

private:
	~Peer() {}

public:
	Peer();

	/**
	 * Construct a new peer
	 *
	 * @param myIdentity Identity of THIS node (for key agreement)
	 * @param peerIdentity Identity of peer
	 * @throws std::runtime_error Key agreement with peer's identity failed
	 */
	Peer(const Identity &myIdentity,const Identity &peerIdentity)
		throw(std::runtime_error);

	/**
	 * @return Time peer record was last used in any way
	 */
	inline uint64_t lastUsed() const throw() { return _lastUsed; }

	/**
	 * @param now New time of last use
	 */
	inline void setLastUsed(uint64_t now) throw() { _lastUsed = now; }

	/**
	 * @return This peer's ZT address (short for identity().address())
	 */
	inline const Address &address() const throw() { return _id.address(); }

	/**
	 * @return This peer's identity
	 */
	inline const Identity &identity() const throw() { return _id; }

	/**
	 * Must be called on authenticated packet receive from this peer
	 * 
	 * @param _r Runtime environment
	 * @param localPort Local port on which packet was received
	 * @param remoteAddr Internet address of sender
	 * @param hops ZeroTier (not IP) hops
	 * @param packetId Packet ID
	 * @param verb Packet verb
	 * @param inRePacketId Packet ID in reply to (for OK/ERROR, 0 otherwise)
	 * @param inReVerb Verb in reply to (for OK/ERROR, VERB_NOP otherwise)
	 * @param now Current time
	 */
	void onReceive(
		const RuntimeEnvironment *_r,
		Demarc::Port localPort,
		const InetAddress &remoteAddr,
		unsigned int hops,
		uint64_t packetId,
		Packet::Verb verb,
		uint64_t inRePacketId,
		Packet::Verb inReVerb,
		uint64_t now);

	/**
	 * Send a UDP packet to this peer directly (not via relaying)
	 * 
	 * @param _r Runtime environment
	 * @param data Data to send
	 * @param len Length of packet
	 * @param now Current time
	 * @return NULL_PORT or port packet was sent from
	 */
	Demarc::Port send(const RuntimeEnvironment *_r,const void *data,unsigned int len,uint64_t now);

	/**
	 * Send firewall opener to active link
	 * 
	 * @param _r Runtime environment
	 * @param now Current time
	 * @return True if send appears successful for at least one address type
	 */
	bool sendFirewallOpener(const RuntimeEnvironment *_r,uint64_t now);

	/**
	 * Send HELLO to a peer via all active direct paths available
	 * 
	 * @param _r Runtime environment
	 * @param now Current time
	 * @return True if send appears successful for at least one address type
	 */
	bool sendPing(const RuntimeEnvironment *_r,uint64_t now);

	/**
	 * Set an address to reach this peer
	 *
	 * @param addr Address to set
	 * @param fixed If true, address is fixed (won't be changed on packet receipt)
	 */
	void setPathAddress(const InetAddress &addr,bool fixed);

	/**
	 * Clear the fixed flag for an address type
	 *
	 * @param t Type to clear, or TYPE_NULL to clear flag on all types
	 */
	void clearFixedFlag(InetAddress::AddressType t);

	/**
	 * @return Last successfully sent firewall opener
	 */
	inline uint64_t lastFirewallOpener() const
		throw()
	{
		return std::max(_ipv4p.lastFirewallOpener,_ipv6p.lastFirewallOpener);
	}

	/**
	 * @return Time of last direct packet receive
	 */
	inline uint64_t lastDirectReceive() const
		throw()
	{
		return std::max(_ipv4p.lastReceive,_ipv6p.lastReceive);
	}

	/**
	 * @return Time of last direct packet send
	 */
	inline uint64_t lastDirectSend() const
		throw()
	{
		return std::max(_ipv4p.lastSend,_ipv6p.lastSend);
	}

	/**
	 * @return Time of most recent unicast frame received
	 */
	inline uint64_t lastUnicastFrame() const
		throw()
	{
		return _lastUnicastFrame;
	}

	/**
	 * @return Time of most recent multicast frame received
	 */
	inline uint64_t lastMulticastFrame() const
		throw()
	{
		return _lastMulticastFrame;
	}

	/**
	 * @return Time of most recent frame of any kind (unicast or multicast)
	 */
	inline uint64_t lastFrame() const
		throw()
	{
		return std::max(_lastUnicastFrame,_lastMulticastFrame);
	}

	/**
	 * @return Time we last announced state TO this peer, such as multicast LIKEs
	 */
	inline uint64_t lastAnnouncedTo() const
		throw()
	{
		return _lastAnnouncedTo;
	}

	/**
	 * @return Current latency or 0 if unknown (max: 65535)
	 */
	inline unsigned int latency() const
		throw()
	{
		unsigned int l = _latency;
		return std::min(l,(unsigned int)65535);
	}

	/**
	 * Update latency with a new direct measurment
	 *
	 * @param l Direct latency measurment in ms
	 */
	inline void addDirectLatencyMeasurment(unsigned int l)
		throw()
	{
		if (l > 65535) l = 65535;
		unsigned int ol = _latency;
		if ((ol > 0)&&(ol < 10000))
			_latency = (ol + l) / 2;
		else _latency = l;
	}

	/**
	 * @return True if this peer has at least one direct IP address path
	 */
	inline bool hasDirectPath() const throw() { return ((_ipv4p.addr)||(_ipv6p.addr)); }

	/**
	 * @param now Current time
	 * @return True if this peer has at least one active or fixed direct path
	 */
	inline bool hasActiveDirectPath(uint64_t now) const throw() { return ((_ipv4p.isActive(now))||(_ipv6p.isActive(now))); }

	/**
	 * @return IPv4 direct address or null InetAddress if none
	 */
	inline InetAddress ipv4Path() const throw() { return _ipv4p.addr; }

	/**
	 * @return IPv6 direct address or null InetAddress if none
	 */
	inline InetAddress ipv6Path() const throw() { return _ipv4p.addr; }

	/**
	 * @return IPv4 direct address or null InetAddress if none
	 */
	inline InetAddress ipv4ActivePath(uint64_t now) const
		throw()
	{
		if (_ipv4p.isActive(now))
			return _ipv4p.addr;
		return InetAddress();
	}

	/**
	 * @return IPv6 direct address or null InetAddress if none
	 */
	inline InetAddress ipv6ActivePath(uint64_t now) const
		throw()
	{
		if (_ipv6p.isActive(now))
			return _ipv6p.addr;
		return InetAddress();
	}

	/**
	 * Forget direct paths
	 *
	 * @param fixedToo If true, also forget 'fixed' paths.
	 */
	inline void forgetDirectPaths(bool fixedToo)
		throw()
	{
		if ((fixedToo)||(!_ipv4p.fixed))
			_ipv4p.addr.zero();
		if ((fixedToo)||(!_ipv6p.fixed))
			_ipv6p.addr.zero();
	}

	/**
	 * @return 256-bit secret symmetric encryption key
	 */
	inline const unsigned char *key() const throw() { return _key; }

	/**
	 * Set the currently known remote version of this peer's client
	 *
	 * @param vmaj Major version
	 * @param vmin Minor version
	 * @param vrev Revision
	 */
	inline void setRemoteVersion(unsigned int vmaj,unsigned int vmin,unsigned int vrev)
	{
		_vMajor = vmaj;
		_vMinor = vmin;
		_vRevision = vrev;
	}

	/**
	 * @return Remote version in string form or '?' if unknown
	 */
	inline std::string remoteVersion() const
	{
		if ((_vMajor)||(_vMinor)||(_vRevision)) {
			char tmp[32];
			Utils::snprintf(tmp,sizeof(tmp),"%u.%u.%u",_vMajor,_vMinor,_vRevision);
			return std::string(tmp);
		}
		return std::string("?");
	}

	/**
	 * @return True if this Peer is initialized with something
	 */
	inline operator bool() const throw() { return (_id); }

	/**
	 * Find a common set of addresses by which two peers can link, if any
	 *
	 * @param a Peer A
	 * @param b Peer B
	 * @param now Current time
	 * @return Pair: B's address to send to A, A's address to send to B
	 */
	static inline std::pair<InetAddress,InetAddress> findCommonGround(const Peer &a,const Peer &b,uint64_t now)
		throw()
	{
		if ((a._ipv6p.isActive(now))&&(b._ipv6p.isActive(now)))
			return std::pair<InetAddress,InetAddress>(b._ipv6p.addr,a._ipv6p.addr);
		else if ((a._ipv4p.isActive(now))&&(b._ipv4p.isActive(now)))
			return std::pair<InetAddress,InetAddress>(b._ipv4p.addr,a._ipv4p.addr);
		else if ((a._ipv6p.addr)&&(b._ipv6p.addr))
			return std::pair<InetAddress,InetAddress>(b._ipv6p.addr,a._ipv6p.addr);
		else if ((a._ipv4p.addr)&&(b._ipv4p.addr))
			return std::pair<InetAddress,InetAddress>(b._ipv4p.addr,a._ipv4p.addr);
		return std::pair<InetAddress,InetAddress>();
	}

	template<unsigned int C>
	inline void serialize(Buffer<C> &b)
	{
		b.append((unsigned char)ZT_PEER_SERIALIZATION_VERSION);
		b.append(_key,sizeof(_key));
		_id.serialize(b,false);
		_ipv4p.serialize(b);
		_ipv6p.serialize(b);
		b.append(_lastUsed);
		b.append(_lastUnicastFrame);
		b.append(_lastMulticastFrame);
		b.append(_lastAnnouncedTo);
		b.append((uint16_t)_vMajor);
		b.append((uint16_t)_vMinor);
		b.append((uint16_t)_vRevision);
		b.append((uint16_t)_latency);
	}
	template<unsigned int C>
	inline unsigned int deserialize(const Buffer<C> &b,unsigned int startAt = 0)
	{
		unsigned int p = startAt;

		if (b[p++] != ZT_PEER_SERIALIZATION_VERSION)
			throw std::invalid_argument("Peer: deserialize(): version mismatch");

		memcpy(_key,b.field(p,sizeof(_key)),sizeof(_key)); p += sizeof(_key);
		p += _id.deserialize(b,p);
		p += _ipv4p.deserialize(b,p);
		p += _ipv6p.deserialize(b,p);
		_lastUsed = b.template at<uint64_t>(p); p += sizeof(uint64_t);
		_lastUnicastFrame = b.template at<uint64_t>(p); p += sizeof(uint64_t);
		_lastMulticastFrame = b.template at<uint64_t>(p); p += sizeof(uint64_t);
		_lastAnnouncedTo = b.template at<uint64_t>(p); p += sizeof(uint64_t);
		_vMajor = b.template at<uint16_t>(p); p += sizeof(uint16_t);
		_vMinor = b.template at<uint16_t>(p); p += sizeof(uint16_t);
		_vRevision = b.template at<uint16_t>(p); p += sizeof(uint16_t);
		_latency = b.template at<uint16_t>(p); p += sizeof(uint16_t);

		return (p - startAt);
	}

private:
	/**
	 * A direct IP path to a peer
	 */
	class WanPath
	{
	public:
		WanPath() :
			lastSend(0),
			lastReceive(0),
			lastFirewallOpener(0),
			localPort(Demarc::ANY_PORT),
			addr(),
			fixed(false)
		{
		}

		inline bool isActive(const uint64_t now) const
			throw()
		{
			return ((addr)&&((fixed)||((now - lastReceive) < ZT_PEER_LINK_ACTIVITY_TIMEOUT)));
		}

		template<unsigned int C>
		inline void serialize(Buffer<C> &b)
			throw(std::out_of_range)
		{
			b.append(lastSend);
			b.append(lastReceive);
			b.append(lastFirewallOpener);
			b.append(Demarc::portToInt(localPort));

			b.append((unsigned char)addr.type());
			switch(addr.type()) {
				case InetAddress::TYPE_NULL:
					break;
				case InetAddress::TYPE_IPV4:
					b.append(addr.rawIpData(),4);
					b.append((uint16_t)addr.port());
					break;
				case InetAddress::TYPE_IPV6:
					b.append(addr.rawIpData(),16);
					b.append((uint16_t)addr.port());
					break;
			}

			b.append(fixed ? (unsigned char)1 : (unsigned char)0);
		}

		template<unsigned int C>
		inline unsigned int deserialize(const Buffer<C> &b,unsigned int startAt = 0)
			throw(std::out_of_range,std::invalid_argument)
		{
			unsigned int p = startAt;

			lastSend = b.template at<uint64_t>(p); p += sizeof(uint64_t);
			lastReceive = b.template at<uint64_t>(p); p += sizeof(uint64_t);
			lastFirewallOpener = b.template at<uint64_t>(p); p += sizeof(uint64_t);
			localPort = Demarc::intToPort(b.template at<uint64_t>(p)); p += sizeof(uint64_t);

			switch ((InetAddress::AddressType)b[p++]) {
				case InetAddress::TYPE_NULL:
					addr.zero();
					break;
				case InetAddress::TYPE_IPV4:
					addr.set(b.field(p,4),4,b.template at<uint16_t>(p + 4));
					p += 4 + sizeof(uint16_t);
					break;
				case InetAddress::TYPE_IPV6:
					addr.set(b.field(p,16),16,b.template at<uint16_t>(p + 16));
					p += 16 + sizeof(uint16_t);
					break;
			}

			fixed = (b[p++] != 0);

			return (p - startAt);
		}

		uint64_t lastSend;
		uint64_t lastReceive;
		uint64_t lastFirewallOpener;
		Demarc::Port localPort; // ANY_PORT if not defined (size: uint64_t)
		InetAddress addr; // null InetAddress if path is undefined
		bool fixed; // do not learn address from received packets
	};

	unsigned char _key[ZT_PEER_SECRET_KEY_LENGTH];
	Identity _id;

	WanPath _ipv4p;
	WanPath _ipv6p;

	volatile uint64_t _lastUsed;
	volatile uint64_t _lastUnicastFrame;
	volatile uint64_t _lastMulticastFrame;
	volatile uint64_t _lastAnnouncedTo;
	volatile unsigned int _vMajor,_vMinor,_vRevision;
	volatile unsigned int _latency;

	AtomicCounter __refCount;
};

} // namespace ZeroTier

// Add a swap() for shared ptr's to peers to speed up peer sorts
namespace std {
	template<>
	inline void swap(ZeroTier::SharedPtr<ZeroTier::Peer> &a,ZeroTier::SharedPtr<ZeroTier::Peer> &b)
	{
		a.swap(b);
	}
}

#endif
