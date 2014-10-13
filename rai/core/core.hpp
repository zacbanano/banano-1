#pragma once

#include <rai/secure.hpp>

#include <boost/asio.hpp>
#include <boost/network/include/http/server.hpp>
#include <boost/network/utils/thread_pool.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/circular_buffer.hpp>

#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace CryptoPP
{
    class SHA3;
}

std::ostream & operator << (std::ostream &, std::chrono::system_clock::time_point const &);
namespace rai {
    using endpoint = boost::asio::ip::udp::endpoint;
    using tcp_endpoint = boost::asio::ip::tcp::endpoint;
    bool parse_endpoint (std::string const &, rai::endpoint &);
    bool parse_tcp_endpoint (std::string const &, rai::tcp_endpoint &);
	bool reserved_address (rai::endpoint const &);
}

namespace std
{
    template <size_t size>
    struct endpoint_hash
    {
    };
    template <>
    struct endpoint_hash <4>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            auto result (endpoint_a.address ().to_v4 ().to_ulong () ^ endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct endpoint_hash <8>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            auto result ((endpoint_a.address ().to_v4 ().to_ulong () << 2) | endpoint_a.port ());
            return result;
        }
    };
    template <>
    struct hash <rai::endpoint>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            endpoint_hash <sizeof (size_t)> ehash;
            return ehash (endpoint_a);
        }
    };
}
namespace boost
{
    template <>
    struct hash <rai::endpoint>
    {
        size_t operator () (rai::endpoint const & endpoint_a) const
        {
            std::hash <rai::endpoint> hash;
            return hash (endpoint_a);
        }
    };
}

namespace rai {
    class client;
    class destructable
    {
    public:
        destructable (std::function <void ()>);
        ~destructable ();
        std::function <void ()> operation;
    };
	class election : public std::enable_shared_from_this <rai::election>
	{
	public:
		election (std::shared_ptr <rai::client>, rai::block const &);
        void start ();
        void vote (rai::vote const &);
        void announce_vote ();
        void timeout_action (std::shared_ptr <rai::destructable>);
		void start_request (rai::block const &);
		rai::uint256_t uncontested_threshold ();
		rai::uint256_t contested_threshold ();
		rai::votes votes;
        std::shared_ptr <rai::client> client;
		std::chrono::system_clock::time_point last_vote;
		bool confirmed;
	};
    class conflicts
    {
    public:
		conflicts (rai::client &);
        void start (rai::block const &, bool);
		void update (rai::vote const &);
        void stop (rai::block_hash const &);
        std::unordered_map <rai::block_hash, std::shared_ptr <rai::election>> roots;
		rai::client & client;
        std::mutex mutex;
    };
    enum class message_type : uint8_t
    {
        invalid,
        not_a_type,
        keepalive_req,
        keepalive_ack,
        publish_req,
        confirm_req,
        confirm_ack,
        confirm_unk,
        bulk_req,
		frontier_req
    };
    class message_visitor;
    class message
    {
    public:
        virtual ~message () = default;
        virtual void serialize (rai::stream &) = 0;
        virtual void visit (rai::message_visitor &) const = 0;
    };
    class keepalive_req : public message
    {
    public:
        void visit (rai::message_visitor &) const override;
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
		std::array <rai::endpoint, 24> peers;
    };
    class keepalive_ack : public message
    {
    public:
        void visit (rai::message_visitor &) const override;
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
		bool operator == (rai::keepalive_ack const &) const;
		std::array <rai::endpoint, 24> peers;
		rai::uint256_union checksum;
    };
    class publish_req : public message
    {
    public:
        publish_req () = default;
        publish_req (std::unique_ptr <rai::block>);
        void visit (rai::message_visitor &) const override;
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        bool operator == (rai::publish_req const &) const;
        rai::uint256_union work;
        std::unique_ptr <rai::block> block;
    };
    class confirm_req : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::confirm_req const &) const;
        rai::uint256_union work;
        std::unique_ptr <rai::block> block;
    };
    class confirm_ack : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::confirm_ack const &) const;
        rai::vote vote;
        rai::uint256_union work;
    };
    class confirm_unk : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
		rai::uint256_union hash () const;
        rai::address rep_hint;
    };
    class frontier_req : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        bool operator == (rai::frontier_req const &) const;
        rai::address start;
        uint32_t age;
        uint32_t count;
    };
    class bulk_req : public message
    {
    public:
        bool deserialize (rai::stream &);
        void serialize (rai::stream &) override;
        void visit (rai::message_visitor &) const override;
        rai::uint256_union start;
        rai::block_hash end;
        uint32_t count;
    };
    class message_visitor
    {
    public:
        virtual void keepalive_req (rai::keepalive_req const &) = 0;
        virtual void keepalive_ack (rai::keepalive_ack const &) = 0;
        virtual void publish_req (rai::publish_req const &) = 0;
        virtual void confirm_req (rai::confirm_req const &) = 0;
        virtual void confirm_ack (rai::confirm_ack const &) = 0;
        virtual void confirm_unk (rai::confirm_unk const &) = 0;
        virtual void bulk_req (rai::bulk_req const &) = 0;
        virtual void frontier_req (rai::frontier_req const &) = 0;
    };
    class key_entry
    {
    public:
        rai::key_entry * operator -> ();
        rai::public_key first;
        rai::private_key second;
    };
    class key_iterator
    {
    public:
        key_iterator (leveldb::DB *); // Begin iterator
        key_iterator (leveldb::DB *, std::nullptr_t); // End iterator
        key_iterator (leveldb::DB *, rai::uint256_union const &);
        key_iterator (rai::key_iterator &&) = default;
        void set_current ();
        key_iterator & operator ++ ();
        rai::key_entry & operator -> ();
        bool operator == (rai::key_iterator const &) const;
        bool operator != (rai::key_iterator const &) const;
        rai::key_entry current;
        std::unique_ptr <leveldb::Iterator> iterator;
    };
    class wallet
    {
    public:
        wallet (boost::filesystem::path const &);
        rai::uint256_union check ();
		bool rekey (rai::uint256_union const &);
        rai::uint256_union wallet_key ();
        void insert (rai::private_key const &);
        bool fetch (rai::public_key const &, rai::private_key &);
        bool generate_send (rai::ledger &, rai::public_key const &, rai::uint256_t const &, std::vector <std::unique_ptr <rai::send_block>> &);
		bool valid_password ();
        key_iterator find (rai::uint256_union const &);
        key_iterator begin ();
        key_iterator end ();
        rai::uint256_union hash_password (std::string const &);
        rai::uint256_union password;
    private:
        leveldb::DB * handle;
    };
    class operation
    {
    public:
        bool operator > (rai::operation const &) const;
        std::chrono::system_clock::time_point wakeup;
        std::function <void ()> function;
    };
    class processor_service
    {
    public:
        processor_service ();
        void run ();
        size_t poll ();
        size_t poll_one ();
        void add (std::chrono::system_clock::time_point const &, std::function <void ()> const &);
        void stop ();
        bool stopped ();
        size_t size ();
    private:
        bool done;
        std::mutex mutex;
        std::condition_variable condition;
        std::priority_queue <operation, std::vector <operation>, std::greater <operation>> operations;
    };
    class peer_information
    {
    public:
        rai::endpoint endpoint;
        std::chrono::system_clock::time_point last_contact;
        std::chrono::system_clock::time_point last_attempt;
    };
    class gap_information
    {
    public:
        std::chrono::system_clock::time_point arrival;
        rai::block_hash hash;
        std::unique_ptr <rai::block> block;
    };
    class gap_cache
    {
    public:
        gap_cache ();
        void add (rai::block const &, rai::block_hash);
        std::unique_ptr <rai::block> get (rai::block_hash const &);
        boost::multi_index_container
        <
            gap_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::hash>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>
            >
        > blocks;
        size_t const max;
    };
    using session = std::function <void (rai::confirm_ack const &, rai::endpoint const &)>;
    class processor
    {
    public:
        processor (rai::client &);
        void stop ();
        void find_network (std::vector <std::pair <std::string, std::string>> const &);
        void bootstrap (rai::tcp_endpoint const &, std::function <void ()> const &);
        rai::process_result process_receive (rai::block const &);
        void process_receive_republish (std::unique_ptr <rai::block>, rai::endpoint const &);
        void republish (std::unique_ptr <rai::block>, rai::endpoint const &);
		void process_message (rai::message &, rai::endpoint const &, bool);
		void process_unknown (rai::vectorstream &);
        void process_confirmation (rai::block const &, rai::endpoint const &);
        void process_confirmed (rai::block const &);
        void ongoing_keepalive ();
        rai::client & client;
        static std::chrono::seconds constexpr period = std::chrono::seconds (10);
        static std::chrono::seconds constexpr cutoff = period * 5;
    };
    class transactions
    {
    public:
        transactions (rai::ledger &, rai::wallet &, rai::processor &);
        bool receive (rai::send_block const &, rai::private_key const &, rai::address const &);
        bool send (rai::address const &, rai::uint256_t const &);
        void vote (rai::vote const &);
		bool rekey (rai::uint256_union const &);
        std::mutex mutex;
        rai::ledger & ledger;
        rai::wallet & wallet;
		rai::processor & processor;
    };
    class bootstrap_initiator : public std::enable_shared_from_this <bootstrap_initiator>
    {
    public:
        bootstrap_initiator (std::shared_ptr <rai::client>, std::function <void ()> const &);
        ~bootstrap_initiator ();
        void run (rai::tcp_endpoint const &);
        void connect_action (boost::system::error_code const &);
        void send_frontier_request ();
        void sent_request (boost::system::error_code const &, size_t);
        void run_receiver ();
        void finish_request ();
        void add_and_send (std::unique_ptr <rai::message>);
        void add_request (std::unique_ptr <rai::message>);
        std::queue <std::unique_ptr <rai::message>> requests;
        std::vector <uint8_t> send_buffer;
        std::shared_ptr <rai::client> client;
        boost::asio::ip::tcp::socket socket;
        std::function <void ()> complete_action;
        std::mutex mutex;
        static size_t const max_queue_size = 10;
    };
    class bulk_req_initiator : public std::enable_shared_from_this <bulk_req_initiator>
    {
    public:
        bulk_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const &, std::unique_ptr <rai::bulk_req>);
        ~bulk_req_initiator ();
        void receive_block ();
        void received_type (boost::system::error_code const &, size_t);
        void received_block (boost::system::error_code const &, size_t);
        bool process_block (rai::block const &);
        bool process_end ();
        std::array <uint8_t, 4000> receive_buffer;
        std::unique_ptr <rai::bulk_req> request;
        rai::block_hash expecting;
        std::shared_ptr <rai::bootstrap_initiator> connection;
    };
    class frontier_req_initiator : public std::enable_shared_from_this <frontier_req_initiator>
    {
    public:
        frontier_req_initiator (std::shared_ptr <rai::bootstrap_initiator> const &, std::unique_ptr <rai::frontier_req>);
        ~frontier_req_initiator ();
        void receive_frontier ();
        void received_frontier (boost::system::error_code const &, size_t);
        std::array <uint8_t, 4000> receive_buffer;
        std::unique_ptr <rai::frontier_req> request;
        std::shared_ptr <rai::bootstrap_initiator> connection;
    };
    class work
    {
    public:
        work ();
        rai::uint256_union generate (rai::uint256_union const &, rai::uint256_union const &);
        rai::uint256_union create (rai::uint256_union const &);
        bool validate (rai::uint256_union const &, rai::uint256_union const &);
        rai::uint256_union threshold_requirement;
        size_t const entry_requirement;
        uint32_t const iteration_requirement;
        std::vector <rai::uint512_union> entries;
    };
    class network
    {
    public:
        network (boost::asio::io_service &, uint16_t, rai::client &);
        void receive ();
        void stop ();
        void receive_action (boost::system::error_code const &, size_t);
        void rpc_action (boost::system::error_code const &, size_t);
        void publish_block (rai::endpoint const &, std::unique_ptr <rai::block>);
        void confirm_block (std::unique_ptr <rai::block>, uint64_t);
        void merge_peers (std::shared_ptr <std::vector <uint8_t>> const &, std::array <rai::endpoint, 24> const &);
        void send_keepalive (rai::endpoint const &);
        void send_confirm_req (rai::endpoint const &, rai::block const &);
        void send_buffer (uint8_t const *, size_t, rai::endpoint const &, std::function <void (boost::system::error_code const &, size_t)>);
        void send_complete (boost::system::error_code const &, size_t);
        rai::endpoint endpoint ();
        rai::endpoint remote;
        std::array <uint8_t, 512> buffer;
        rai::work work;
        boost::asio::ip::udp::socket socket;
        boost::asio::io_service & service;
        rai::client & client;
        std::queue <std::tuple <uint8_t const *, size_t, rai::endpoint, std::function <void (boost::system::error_code const &, size_t)>>> sends;
        std::mutex mutex;
        uint64_t keepalive_req_count;
        uint64_t keepalive_ack_count;
        uint64_t publish_req_count;
        uint64_t confirm_req_count;
        uint64_t confirm_ack_count;
        uint64_t confirm_unk_count;
        uint64_t bad_sender_count;
        uint64_t unknown_count;
        uint64_t error_count;
        uint64_t insufficient_work_count;
        bool on;
    };
    class bootstrap_receiver
    {
    public:
        bootstrap_receiver (boost::asio::io_service &, uint16_t, rai::client &);
        void start ();
        void stop ();
        void accept_connection ();
        void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
        rai::tcp_endpoint endpoint ();
        boost::asio::ip::tcp::acceptor acceptor;
        rai::tcp_endpoint local;
        boost::asio::io_service & service;
        rai::client & client;
        bool on;
    };
    class bootstrap_connection : public std::enable_shared_from_this <bootstrap_connection>
    {
    public:
        bootstrap_connection (std::shared_ptr <boost::asio::ip::tcp::socket>, std::shared_ptr <rai::client>);
        ~bootstrap_connection ();
        void receive ();
        void receive_type_action (boost::system::error_code const &, size_t);
        void receive_bulk_req_action (boost::system::error_code const &, size_t);
		void receive_frontier_req_action (boost::system::error_code const &, size_t);
		void add_request (std::unique_ptr <rai::message>);
		void finish_request ();
		void run_next ();
        std::array <uint8_t, 128> receive_buffer;
        std::shared_ptr <boost::asio::ip::tcp::socket> socket;
        std::shared_ptr <rai::client> client;
        std::mutex mutex;
        std::queue <std::unique_ptr <rai::message>> requests;
    };
    class bulk_req_response : public std::enable_shared_from_this <bulk_req_response>
    {
    public:
        bulk_req_response (std::shared_ptr <rai::bootstrap_connection> const &, std::unique_ptr <rai::bulk_req>);
        void set_current_end ();
        std::unique_ptr <rai::block> get_next ();
        void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::shared_ptr <rai::bootstrap_connection> connection;
        std::unique_ptr <rai::bulk_req> request;
        std::vector <uint8_t> send_buffer;
        rai::block_hash current;
    };
    class frontier_req_response : public std::enable_shared_from_this <frontier_req_response>
    {
    public:
        frontier_req_response (std::shared_ptr <rai::bootstrap_connection> const &, std::unique_ptr <rai::frontier_req>);
        void skip_old ();
		void send_next ();
        void sent_action (boost::system::error_code const &, size_t);
        void send_finished ();
        void no_block_sent (boost::system::error_code const &, size_t);
        std::pair <rai::uint256_union, rai::uint256_union> get_next ();
		account_iterator iterator;
        std::shared_ptr <rai::bootstrap_connection> connection;
        std::unique_ptr <rai::frontier_req> request;
        std::vector <uint8_t> send_buffer;
        size_t count;
    };
    class rpc
    {
    public:
		rpc (boost::shared_ptr <boost::asio::io_service>, boost::shared_ptr <boost::network::utils::thread_pool>, uint16_t, rai::client &, std::unordered_set <rai::uint256_union> const &);
        void start ();
        void stop ();
        boost::network::http::server <rai::rpc> server;
        void operator () (boost::network::http::server <rai::rpc>::request const &, boost::network::http::server <rai::rpc>::response &);
        void log (const char *) {}
        rai::client & client;
        bool on;
		std::unordered_set <rai::uint256_union> api_keys;
    };
    class peer_container
    {
    public:
		peer_container (rai::endpoint const &);
        bool known_peer (rai::endpoint const &);
        void incoming_from_peer (rai::endpoint const &);
		bool contacting_peer (rai::endpoint const &);
		void random_fill (std::array <rai::endpoint, 24> &);
        std::vector <peer_information> list ();
        void refresh_action ();
        void queue_next_refresh ();
        std::vector <rai::peer_information> purge_list (std::chrono::system_clock::time_point const &);
        size_t size ();
        bool empty ();
        std::mutex mutex;
		rai::endpoint self;
        boost::multi_index_container
        <peer_information,
            boost::multi_index::indexed_by
            <
                boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, rai::endpoint, &peer_information::endpoint>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
                boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
            >
        > peers;
    };
    extern rai::keypair test_genesis_key;
    extern rai::address rai_test_address;
    extern rai::address rai_live_address;
    extern rai::address genesis_address;
    class genesis
    {
    public:
        explicit genesis ();
        void initialize (rai::block_store &) const;
        rai::block_hash hash () const;
        rai::send_block send1;
        rai::send_block send2;
        rai::open_block open;
    };
    class log
    {
    public:
        log ();
        void add (std::string const &);
        void dump_cerr ();
        boost::circular_buffer <std::pair <std::chrono::system_clock::time_point, std::string>> items;
    };
    class client : public std::enable_shared_from_this <rai::client>
    {
    public:
        client (boost::shared_ptr <boost::asio::io_service>, uint16_t, boost::filesystem::path const &, rai::processor_service &, rai::address const &);
        client (boost::shared_ptr <boost::asio::io_service>, uint16_t, rai::processor_service &, rai::address const &);
        ~client ();
        bool send (rai::public_key const &, rai::uint256_t const &);
        rai::uint256_t balance ();
        void start ();
        void stop ();
        std::shared_ptr <rai::client> shared ();
        bool is_representative ();
		void representative_vote (rai::election &, rai::block const &);
        uint64_t scale_down (rai::uint256_t const &);
        rai::uint256_t scale_up (uint64_t);
        rai::log log;
        rai::address representative;
        rai::block_store store;
        rai::gap_cache gap_cache;
        rai::ledger ledger;
        rai::conflicts conflicts;
        rai::wallet wallet;
        rai::network network;
        rai::bootstrap_receiver bootstrap;
        rai::processor processor;
        rai::transactions transactions;
        rai::peer_container peers;
        rai::processor_service & service;
        rai::uint256_t scale;
    };
    class system
    {
    public:
        system (uint16_t, size_t);
        ~system ();
        void generate_activity (rai::client &);
        void generate_mass_activity (uint32_t, rai::client &);
        void generate_usage_traffic (uint32_t, uint32_t, size_t);
        void generate_usage_traffic (uint32_t, uint32_t);
        rai::uint256_t get_random_amount (rai::client &);
        void generate_send_new (rai::client &);
        void generate_send_existing (rai::client &);
        boost::shared_ptr <boost::asio::io_service> service;
        rai::processor_service processor;
        std::vector <std::shared_ptr <rai::client>> clients;
    };
}