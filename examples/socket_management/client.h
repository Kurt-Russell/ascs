#ifndef _CLIENT_H_
#define _CLIENT_H_

extern bool add_link(const std::string& name, uint_fast64_t id);
extern bool del_link(const std::string& name);
extern uint_fast64_t find_link(const std::string& name);
extern uint_fast64_t find_and_del_link(const std::string& name);

class my_client_socket : public client_socket
{
public:
	my_client_socket(asio::io_context& io_context_) : client_socket(io_context_)
	{
		std::dynamic_pointer_cast<prefix_suffix_packer>(packer())->prefix_suffix("", "\n");
		std::dynamic_pointer_cast<prefix_suffix_unpacker>(unpacker())->prefix_suffix("", "\n");
	}

	void name(const std::string& name_) {_name = name_;}
	const std::string& name() const {return _name;}

protected:
	//msg handling
	virtual bool on_msg_handle(out_msg_type& msg) {printf("received: %s, I'm %s\n", msg.data(), _name.data()); return true;}
	//msg handling end

	virtual void on_recv_error(const asio::error_code& ec) {del_link(_name); client_socket::on_recv_error(ec);}

	virtual void after_close() {} //don't perform reconnecting

private:
	std::string _name;
};

class my_client : public multi_client_base<my_client_socket>
{
public:
	my_client(service_pump& service_pump_) : multi_client_base<my_client_socket>(service_pump_) {}

	bool add_link(const std::string& name)
	{
		auto socket_ptr = create_object();
		assert(socket_ptr);

		if (::add_link(name, socket_ptr->id()))
		{
			//socket_ptr->set_server_addr(9527, "127.0.0.1"); //if you want to set server ip, do it at here like this
			if (!add_socket(socket_ptr)) //exceed ST_ASIO_MAX_OBJECT_NUM
				::del_link(name);
			else
			{
				socket_ptr->name(name);
				return true;
			}
		}

		return false;
	}

	bool del_link(const std::string& name)
	{
		auto socket_ptr = find(find_and_del_link(name));
		return socket_ptr ? (socket_ptr->force_shutdown(false), true) : false;
	}

	bool send_msg(const std::string& name, const std::string& msg)
	{
		auto socket_ptr = find(find_link(name));
		return socket_ptr ?  socket_ptr->send_msg(msg) : false;
	}
};

#endif //#define _CLIENT_H_
