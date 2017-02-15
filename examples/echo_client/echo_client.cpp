
#include <iostream>

//configuration
#define ASCS_SERVER_PORT		9527
//#define ASCS_REUSE_OBJECT //use objects pool
//#define ASCS_FORCE_TO_USE_MSG_RECV_BUFFER //force to use the msg recv buffer
//#define ASCS_CLEAR_OBJECT_INTERVAL	1
//#define ASCS_WANT_MSG_SEND_NOTIFY
#define ASCS_FULL_STATISTIC //full statistic will slightly impact efficiency.
//configuration

//use the following macro to control the type of packer and unpacker
#define PACKER_UNPACKER_TYPE	0
//0-default packer and unpacker, head(length) + body
//1-default replaceable_packer and replaceable_unpacker, head(length) + body
//2-fixed length unpacker
//3-prefix and suffix packer and unpacker

#if 1 == PACKER_UNPACKER_TYPE
#define ASCS_DEFAULT_PACKER replaceable_packer
#define ASCS_DEFAULT_UNPACKER replaceable_unpacker
#elif 2 == PACKER_UNPACKER_TYPE
#define ASCS_DEFAULT_UNPACKER fixed_length_unpacker
#elif 3 == PACKER_UNPACKER_TYPE
#define ASCS_DEFAULT_PACKER prefix_suffix_packer
#define ASCS_DEFAULT_UNPACKER prefix_suffix_unpacker
#endif

#include <ascs/ext/tcp.h>
using namespace ascs;
using namespace ascs::tcp;
using namespace ascs::ext;
using namespace ascs::ext::tcp;

#ifdef _MSC_VER
#define atoll _atoi64
#endif

#define QUIT_COMMAND	"quit"
#define RESTART_COMMAND	"restart"
#define LIST_ALL_CLIENT	"list_all_client"
#define LIST_STATUS		"status"
#define SUSPEND_COMMAND	"suspend"
#define RESUME_COMMAND	"resume"

static bool check_msg;

///////////////////////////////////////////////////
//msg sending interface
#define TCP_RANDOM_SEND_MSG(FUNNAME, SEND_FUNNAME) \
void FUNNAME(const char* const pstr[], const size_t len[], size_t num, bool can_overflow = false) \
{ \
	auto index = (size_t) ((uint64_t) rand() * (size() - 1) / RAND_MAX); \
	at(index)->SEND_FUNNAME(pstr, len, num, can_overflow); \
} \
TCP_SEND_MSG_CALL_SWITCH(FUNNAME, void)
//msg sending interface
///////////////////////////////////////////////////

class echo_socket : public connector
{
public:
	echo_socket(asio::io_service& io_service_) : connector(io_service_), recv_bytes(0), recv_index(0)
	{
#if 2 == PACKER_UNPACKER_TYPE
		std::dynamic_pointer_cast<ASCS_DEFAULT_UNPACKER>(inner_unpacker())->fixed_length(1024);
#elif 3 == PACKER_UNPACKER_TYPE
		std::dynamic_pointer_cast<ASCS_DEFAULT_PACKER>(inner_packer())->prefix_suffix("begin", "end");
		std::dynamic_pointer_cast<ASCS_DEFAULT_UNPACKER>(inner_unpacker())->prefix_suffix("begin", "end");
#endif
	}

	uint64_t get_recv_bytes() const {return recv_bytes;}
	operator uint64_t() const {return recv_bytes;}

	void clear_status() {recv_bytes = recv_index = 0;}
	void begin(size_t msg_num_, size_t msg_len, char msg_fill)
	{
		clear_status();
		msg_num = msg_num_;

		auto buff = new char[msg_len];
		memset(buff, msg_fill, msg_len);
		memcpy(buff, &recv_index, sizeof(size_t)); //seq

#if 2 == PACKER_UNPACKER_TYPE
		//we don't have fixed_length_packer, so use packer instead, but need to pack msgs with native manner.
		send_native_msg(buff, msg_len);
#else
		send_msg(buff, msg_len);
#endif

		delete[] buff;
	}

protected:
	//msg handling
#ifndef ASCS_FORCE_TO_USE_MSG_RECV_BUFFER //not force to use msg recv buffer(so on_msg will make the decision)
	//we can handle msg very fast, so we don't use recv buffer(return true)
	virtual bool on_msg(out_msg_type& msg) {handle_msg(msg); return true;}
#endif
	//we should handle msg in on_msg_handle for time-consuming task like this:
	virtual bool on_msg_handle(out_msg_type& msg, bool link_down) {handle_msg(msg); return true;}
	//msg handling end

#ifdef ASCS_WANT_MSG_SEND_NOTIFY
	virtual void on_msg_send(in_msg_type& msg)
	{
		if (0 == --msg_num)
			return;

		auto pstr = inner_packer()->raw_data(msg);
		auto msg_len = inner_packer()->raw_data_len(msg);
#if 2 == PACKER_UNPACKER_TYPE
		//we don't have fixed_length_packer, so use packer instead, but need to pack msgs with native manner.
		std::advance(pstr, -ASCS_HEAD_LEN);
		msg_len += ASCS_HEAD_LEN;
#endif

		size_t send_index;
		memcpy(&send_index, pstr, sizeof(size_t));
		++send_index;
		memcpy(pstr, &send_index, sizeof(size_t)); //seq

#if 2 == PACKER_UNPACKER_TYPE
		//we don't have fixed_length_packer, so use packer instead, but need to pack msgs with native manner.
		send_native_msg(pstr, msg_len);
#else
		send_msg(pstr, msg_len);
#endif
	}
#endif

private:
	void handle_msg(out_msg_ctype& msg)
	{
		recv_bytes += msg.size();
		if (check_msg && (msg.size() < sizeof(size_t) || 0 != memcmp(&recv_index, msg.data(), sizeof(size_t))))
			printf("check msg error: " ASCS_SF ".\n", recv_index);
		++recv_index;
	}

private:
	uint64_t recv_bytes;
	size_t recv_index, msg_num;
};

class test_client : public client_base<echo_socket>
{
public:
	test_client(service_pump& service_pump_) : client_base<echo_socket>(service_pump_) {}

	uint64_t get_recv_bytes()
	{
		uint64_t total_recv_bytes = 0;
		do_something_to_all([&total_recv_bytes](const auto& item) {total_recv_bytes += *item;});
//		do_something_to_all([&total_recv_bytes](const auto& item) {total_recv_bytes += item->get_recv_bytes();});

		return total_recv_bytes;
	}

	echo_socket::statistic get_statistic()
	{
		echo_socket::statistic stat;
		do_something_to_all([&stat](const auto& item) {stat += item->get_statistic(); });

		return stat;
	}

	void clear_status() {do_something_to_all([](const auto& item) {item->clear_status();});}
	void begin(size_t msg_num, size_t msg_len, char msg_fill) {do_something_to_all([=](const auto& item) {item->begin(msg_num, msg_len, msg_fill);});}

	void shutdown_some_client(size_t n)
	{
		static auto test_index = -1;
		++test_index;

		switch (test_index % 6)
		{
#ifdef ASCS_CLEAR_OBJECT_INTERVAL
			//method #1
			//notice: these methods need to define ASCS_CLEAR_OBJECT_INTERVAL macro, because it just shut down the socket,
			//not really remove them from object pool, this will cause test_client still send data via them, and wait responses from them.
			//for this scenario, the smaller ASCS_CLEAR_OBJECT_INTERVAL macro is, the better experience you will get, so set it to 1 second.
		case 0: do_something_to_one([&n](const auto& item) {return n-- > 0 ? item->graceful_shutdown(), false : true;});				break;
		case 1: do_something_to_one([&n](const auto& item) {return n-- > 0 ? item->graceful_shutdown(false, false), false : true;});	break;
		case 2: do_something_to_one([&n](const auto& item) {return n-- > 0 ? item->force_shutdown(), false : true;});					break;
#else
			//method #2
			//this is a equivalence of calling i_server::del_client in server_socket_base::on_recv_error(see server_socket_base for more details).
		case 0: while (n-- > 0) graceful_shutdown(at(0));			break;
		case 1: while (n-- > 0) graceful_shutdown(at(0), false);	break;
		case 2: while (n-- > 0) force_shutdown(at(0));				break;
#endif
			//if you just want to reconnect to the server, you should do it like this:
		case 3: do_something_to_one([&n](const auto& item) {return n-- > 0 ? item->graceful_shutdown(true), false : true;});			break;
		case 4: do_something_to_one([&n](const auto& item) {return n-- > 0 ? item->graceful_shutdown(true, false), false : true;});	break;
		case 5: do_something_to_one([&n](const auto& item) {return n-- > 0 ? item->force_shutdown(true), false : true;});				break;
		}
	}

	///////////////////////////////////////////////////
	//msg sending interface
	//guarantee send msg successfully even if can_overflow equal to false, success at here just means putting the msg into tcp::socket's send buffer successfully
	TCP_RANDOM_SEND_MSG(safe_random_send_msg, safe_send_msg)
	TCP_RANDOM_SEND_MSG(safe_random_send_native_msg, safe_send_native_msg)
	//msg sending interface
	///////////////////////////////////////////////////
};

int main(int argc, const char* argv[])
{
	printf("usage: test_client [<service thread number=1> [<port=%d> [<ip=%s> [link num=16]]]]\n", ASCS_SERVER_PORT, ASCS_SERVER_IP);
	if (argc >= 2 && (0 == strcmp(argv[1], "--help") || 0 == strcmp(argv[1], "-h")))
		return 0;
	else
		puts("type " QUIT_COMMAND " to end.");

	///////////////////////////////////////////////////////////
	size_t link_num = 16;
	if (argc > 4)
		link_num = std::min(ASCS_MAX_OBJECT_NUM, std::max(atoi(argv[4]), 1));

	printf("exec: test_client with " ASCS_SF " links\n", link_num);
	///////////////////////////////////////////////////////////

	service_pump sp;
	test_client client(sp);
	//echo client means to cooperate with echo server while doing performance test, it will not send msgs back as echo server does,
	//otherwise, dead loop will occur, network resource will be exhausted.

//	argv[2] = "::1" //ipv6
//	argv[2] = "127.0.0.1" //ipv4
	unsigned short port = argc > 2 ? atoi(argv[2]) : ASCS_SERVER_PORT;
	std::string ip = argc > 3 ? argv[3] : ASCS_SERVER_IP;

	//method #1, create and add clients manually.
	auto client_ptr = client.create_object();
	//client_ptr->set_server_addr(port, ip); //we don't have to set server address at here, the following do_something_to_all will do it for us
	//some other initializations according to your business
	client.add_socket(client_ptr, false);
	client_ptr.reset(); //important, otherwise, st_object_pool will not be able to free or reuse this object.

	//method #2, add clients first without any arguments, then set the server address.
	for (size_t i = 1; i < link_num / 2; ++i)
		client.add_client();
	client.do_something_to_all([argv, port, &ip](const auto& item) {item->set_server_addr(port, ip);});

	//method #3, add clients and set server address in one invocation.
	for (auto i = std::max((size_t) 1, link_num / 2); i < link_num; ++i)
		client.add_client(port, ip);

	auto thread_num = 1;
	if (argc > 1)
		thread_num = std::min(16, std::max(thread_num, atoi(argv[1])));
#ifdef ASCS_CLEAR_OBJECT_INTERVAL
	if (1 == thread_num)
		++thread_num;
#endif

	sp.start_service(thread_num);
	while(sp.is_running())
	{
		std::string str;
		std::getline(std::cin, str);
		if (QUIT_COMMAND == str)
			sp.stop_service();
		else if (RESTART_COMMAND == str)
		{
			sp.stop_service();
			sp.start_service(thread_num);
		}
		else if (LIST_STATUS == str)
		{
			printf("link #: " ASCS_SF ", valid links: " ASCS_SF ", invalid links: " ASCS_SF "\n", client.size(), client.valid_size(), client.invalid_object_size());
			puts("");
			puts(client.get_statistic().to_string().data());
		}
		//the following two commands demonstrate how to suspend msg dispatching, no matter recv buffer been used or not
		else if (SUSPEND_COMMAND == str)
			client.do_something_to_all([](const auto& item) {item->suspend_dispatch_msg(true);});
		else if (RESUME_COMMAND == str)
			client.do_something_to_all([](const auto& item) {item->suspend_dispatch_msg(false);});
		else if (LIST_ALL_CLIENT == str)
			client.list_all_object();
		else if (!str.empty())
		{
			if ('+' == str[0] || '-' == str[0])
			{
				auto n = (size_t) atoi(std::next(str.data()));
				if (0 == n)
					n = 1;

				if ('+' == str[0])
					for (; n > 0 && client.add_client(port, ip); --n);
				else
				{
					if (n > client.size())
						n = client.size();

					client.shutdown_some_client(n);
				}

				link_num = client.size();
				continue;
			}

#ifdef ASCS_CLEAR_OBJECT_INTERVAL
			link_num = client.size();
			if (link_num != client.valid_size())
			{
				puts("please wait for a while, because st_object_pool has not cleaned up invalid links.");
				continue;
			}
#endif
			size_t msg_num = 1024;
			size_t msg_len = 1024; //must greater than or equal to sizeof(size_t)
			auto msg_fill = '0';
			char model = 0; //0 broadcast, 1 randomly pick one link per msg

			auto parameters = split_string(str);
			auto iter = std::begin(parameters);
			if (iter != std::end(parameters)) msg_num = std::max((size_t) atoll(iter++->data()), (size_t) 1);

#if 1 == PACKER_UNPACKER_TYPE
			if (iter != std::end(parameters)) msg_len = std::min(packer::get_max_msg_size(),
				std::max((size_t) atoi(iter++->data()), sizeof(size_t))); //include seq
#elif 2 == PACKER_UNPACKER_TYPE
			if (iter != std::end(parameters)) ++iter;
			msg_len = 1024; //we hard code this because we fixedly initialized the length of fixed_length_unpacker to 1024
#elif 3 == PACKER_UNPACKER_TYPE
			if (iter != std::end(parameters)) msg_len = std::min((size_t) ASCS_MSG_BUFFER_SIZE,
				std::max((size_t) atoi(iter++->data()), sizeof(size_t)));
#endif
			if (iter != std::end(parameters)) msg_fill = *iter++->data();
			if (iter != std::end(parameters)) model = *iter++->data() - '0';

			unsigned percent = 0;
			uint64_t total_msg_bytes;
			switch (model)
			{
			case 0:
				check_msg = true;
				total_msg_bytes = msg_num * link_num; break;
			case 1:
				check_msg = false;
				srand(time(nullptr));
				total_msg_bytes = msg_num; break;
			default:
				total_msg_bytes = 0; break;
			}

			if (total_msg_bytes > 0)
			{
				printf("test parameters after adjustment: " ASCS_SF " " ASCS_SF " %c %d\n", msg_num, msg_len, msg_fill, model);
				puts("performance test begin, this application will have no response during the test!");

				client.clear_status();
				total_msg_bytes *= msg_len;
				ascs_cpu_timer begin_time;

#ifdef ASCS_WANT_MSG_SEND_NOTIFY
				if (0 == model)
				{
					client.begin(msg_num, msg_len, msg_fill);

					unsigned new_percent = 0;
					do
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(50));

						new_percent = (unsigned) (100 * client.get_recv_bytes() / total_msg_bytes);
						if (percent != new_percent)
						{
							percent = new_percent;
							printf("\r%u%%", percent);
							fflush(stdout);
						}
					} while (100 != new_percent);

					printf("\r100%%\ntime spent statistics: %.1f seconds.\n", begin_time.elapsed());
					printf("speed: %.0f(*2)kB/s.\n", total_msg_bytes / begin_time.elapsed() / 1024);
				}
				else
					puts("if ASCS_WANT_MSG_SEND_NOTIFY defined, only support model 0!");
#else
				auto buff = new char[msg_len];
				memset(buff, msg_fill, msg_len);
				uint64_t send_bytes = 0;
				for (size_t i = 0; i < msg_num; ++i)
				{
					memcpy(buff, &i, sizeof(size_t)); //seq

					switch (model)
					{
					case 0:
#if 2 == PACKER_UNPACKER_TYPE
						//we don't have fixed_length_packer, so use packer instead, but need to pack msgs with native manner.
						client.safe_broadcast_native_msg(buff, msg_len);
#else
						client.safe_broadcast_msg(buff, msg_len);
#endif
						send_bytes += link_num * msg_len;
						break;
					case 1:
#if 2 == PACKER_UNPACKER_TYPE
						//we don't have fixed_length_packer, so use packer instead, but need to pack msgs with native manner.
						client.safe_random_send_native_msg(buff, msg_len);
#else
						client.safe_random_send_msg(buff, msg_len);
#endif
						send_bytes += msg_len;
						break;
					default:
						break;
					}

					auto new_percent = (unsigned) (100 * send_bytes / total_msg_bytes);
					if (percent != new_percent)
					{
						percent = new_percent;
						printf("\r%u%%", percent);
						fflush(stdout);
					}
				}
				delete[] buff;

				while(client.get_recv_bytes() != total_msg_bytes)
					std::this_thread::sleep_for(std::chrono::milliseconds(50));

				printf("\r100%%\ntime spent statistics: %.1f seconds.\n", begin_time.elapsed());
				printf("speed: %.0f(*2)kB/s.\n", total_msg_bytes / begin_time.elapsed() / 1024);
#endif
			} // if (total_data_num > 0)
		}
	}

    return 0;
}

//restore configuration
#undef ASCS_SERVER_PORT
#undef ASCS_REUSE_OBJECT
#undef ASCS_FORCE_TO_USE_MSG_RECV_BUFFER
#undef ASCS_CLEAR_OBJECT_INTERVAL
#undef ASCS_WANT_MSG_SEND_NOTIFY
#undef ASCS_FULL_STATISTIC
#undef ASCS_DEFAULT_PACKER
#undef ASCS_DEFAULT_UNPACKER
//restore configuration