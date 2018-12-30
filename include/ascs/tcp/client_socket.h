/*
 * client_socket.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * this class only used at client endpoint
 */

#ifndef _ASCS_CLIENT_SOCKET_H_
#define _ASCS_CLIENT_SOCKET_H_

#include "socket.h"

namespace ascs { namespace tcp {

template <typename Packer, typename Unpacker, typename Socket = asio::ip::tcp::socket,
	template<typename, typename> class InQueue = ASCS_INPUT_QUEUE, template<typename> class InContainer = ASCS_INPUT_CONTAINER,
	template<typename, typename> class OutQueue = ASCS_OUTPUT_QUEUE, template<typename> class OutContainer = ASCS_OUTPUT_CONTAINER>
class client_socket_base : public socket_base<Socket, Packer, Unpacker, InQueue, InContainer, OutQueue, OutContainer>
{
private:
	typedef socket_base<Socket, Packer, Unpacker, InQueue, InContainer, OutQueue, OutContainer> super;

public:
	static const typename super::tid TIMER_BEGIN = super::TIMER_END;
	static const typename super::tid TIMER_CONNECT = TIMER_BEGIN;
	static const typename super::tid TIMER_END = TIMER_BEGIN + 5;

	client_socket_base(asio::io_context& io_context_) : super(io_context_), need_reconnect(true) {set_server_addr(ASCS_SERVER_PORT, ASCS_SERVER_IP);}
	template<typename Arg>
	client_socket_base(asio::io_context& io_context_, Arg& arg) : super(io_context_, arg), need_reconnect(true) {set_server_addr(ASCS_SERVER_PORT, ASCS_SERVER_IP);}

	//reset all, be ensure that no operations performed on this socket when invoke it, subclass must rewrite this function to initialize itself, and then
	// call superclass' reset function, before reusing this socket, object_pool will invoke this function
	virtual void reset() {need_reconnect = true; super::reset();}

	bool set_server_addr(unsigned short port, const std::string& ip = ASCS_SERVER_IP)
	{
		asio::error_code ec;
#if ASIO_VERSION >= 101100
		auto addr = asio::ip::make_address(ip, ec);
#else
		auto addr = asio::ip::address::from_string(ip, ec);
#endif
		if (ec)
			return false;

		server_addr = asio::ip::tcp::endpoint(addr, port);
		return true;
	}
	const asio::ip::tcp::endpoint& get_server_addr() const {return server_addr;}

	//if you don't want to reconnect to the server after link broken, call close_reconnect() or rewrite after_close() virtual function and do nothing in it,
	//if you want to control the retry times and delay time after reconnecting failed, rewrite prepare_reconnect virtual function.
	//disconnect(bool), force_shutdown(bool) and graceful_shutdown(bool, bool) can overwrite reconnecting behavior, please note.
	//reset() virtual function will open reconnecting, please note.
	void open_reconnect() {need_reconnect = true;}
	void close_reconnect() {need_reconnect = false;}

	//if the connection is broken unexpectedly, client_socket_base will try to reconnect to the server automatically (if need_reconnect is true).
	void disconnect(bool reconnect = false) {force_shutdown(reconnect);}
	void force_shutdown(bool reconnect = false)
	{
		if (super::link_status::FORCE_SHUTTING_DOWN != this->status)
			this->show_info("client link:", "been shut down.");

		need_reconnect = reconnect;
		super::force_shutdown();
	}

	//sync must be false if you call graceful_shutdown in on_msg
	//furthermore, you're recommended to call this function with sync equal to false in all service threads,
	//all callbacks will be called in service threads.
	//this function is not thread safe, please note.
	void graceful_shutdown(bool reconnect = false, bool sync = true)
	{
		if (this->is_broken())
			return force_shutdown(reconnect);
		else if (!this->is_shutting_down())
			this->show_info("client link:", "being shut down gracefully.");

		need_reconnect = reconnect;
		super::graceful_shutdown(sync);
	}

protected:
	virtual bool do_start() //connect
	{
		assert(!this->is_connected());

		this->lowest_layer().async_connect(server_addr, this->make_handler_error([this](const asio::error_code& ec) {this->connect_handler(ec);}));
		return true;
	}

	virtual void connect_handler(const asio::error_code& ec)
	{
		if (!ec) //already started, so cannot call start()
			super::do_start();
		else if (need_reconnect)
			prepare_next_reconnect(ec);
	}

	//after how much time (ms), client_socket_base will try to reconnect the server, negative value means give up.
	virtual int prepare_reconnect(const asio::error_code& ec) {return ASCS_RECONNECT_INTERVAL;}
	virtual void on_connect() {unified_out::info_out("connecting success.");}
	virtual void on_unpack_error() {unified_out::info_out("can not unpack msg."); force_shutdown();}
	virtual void on_recv_error(const asio::error_code& ec)
	{
		this->show_info("client link:", "broken/been shut down", ec);

		force_shutdown(need_reconnect);
		this->status = super::link_status::BROKEN;
	}

	virtual void on_async_shutdown_error() {force_shutdown(need_reconnect);}
	virtual bool on_heartbeat_error()
	{
		this->show_info("client link:", "broke unexpectedly.");

		force_shutdown(need_reconnect);
		return false;
	}

	//reconnect at here rather than in on_recv_error to make sure no async invocations performed on this socket before reconnecting.
	//if you don't want to reconnect the server after link broken, rewrite this virtual function and do nothing in it or call close_reconnt().
	//if you want to control the retry times and delay time after reconnecting failed, rewrite prepare_reconnect virtual function.
	virtual void after_close() {if (need_reconnect) this->start();}

	bool prepare_next_reconnect(const asio::error_code& ec)
	{
		if (this->started() && !this->stopped())
		{
#ifdef _WIN32
			if (asio::error::connection_refused != ec && asio::error::network_unreachable != ec && asio::error::timed_out != ec)
#endif
			{
				asio::error_code ec;
				this->lowest_layer().close(ec);
			}

			auto delay = prepare_reconnect(ec);
			if (delay >= 0)
			{
				this->set_timer(TIMER_CONNECT, delay, [this](typename super::tid id)->bool {this->do_start(); return false;});
				return true;
			}
		}

		return false;
	}

protected:
	bool need_reconnect;

private:
	asio::ip::tcp::endpoint server_addr;
};

}} //namespace

#endif /* _ASCS_CLIENT_SOCKET_H_ */
