#include <iostream>
#include <sstream>

#if TD_DARWIN || TD_LINUX
    #include <unistd.h>
    #include <fcntl.h>
#endif



#include <webserver/test_node.hpp>

using td::Ref;

int verbosity;

template <std::size_t size>
std::ostream& operator<<(std::ostream& stream, const td::UInt<size>& x) {
  for (size_t i = 0; i < size / 8; i++) {
    stream << td::format::hex_digit((x.raw[i] >> 4) & 15) << td::format::hex_digit(x.raw[i] & 15);
  }

  return stream;
}

std::unique_ptr<ton::AdnlExtClient::Callback> TestNode::make_callback() {
  class Callback : public ton::AdnlExtClient::Callback {
   public:
    void on_ready() override {
      td::actor::send_closure(id_, &TestNode::conn_ready);
    }
    void on_stop_ready() override {
      td::actor::send_closure(id_, &TestNode::conn_closed);
    }
    Callback(td::actor::ActorId<TestNode> id) : id_(std::move(id)) {
    }

   private:
    td::actor::ActorId<TestNode> id_;
  };
  return std::make_unique<Callback>(actor_id(this));
}

td::Status TestNode::save_db_file(ton::FileHash file_hash, td::BufferSlice data) {
    std::string fname = block::compute_db_filename(db_root_ + '/', file_hash);
    for (int i = 0; i < 10; i++) {
        std::string tmp_fname = block::compute_db_tmp_filename(db_root_ + '/', file_hash, i);
        auto res = block::save_binary_file(tmp_fname, data);
        if (res.is_ok()) {
            if (rename(tmp_fname.c_str(), fname.c_str()) < 0) {
                int err = errno;
                LOG(ERROR) << "cannot rename " << tmp_fname << " to " << fname << " : " << strerror(err);
                return td::Status::Error(std::string{"cannot rename file: "} + strerror(err));
            } else {
                LOG(INFO) << data.size() << " bytes saved into file " << fname;
                return td::Status::OK();
            }
        } else if (i == 9) {
            return res;
        }
    }
    return td::Status::Error("cannot save data file");
}

bool unpack_addr(std::ostream& os, Ref<vm::CellSlice> csr) {
    ton::WorkchainId wc;
    ton::StdSmcAddress addr;
    if (!block::tlb::t_MsgAddressInt.extract_std_address(std::move(csr), wc, addr)) {
        os << "<cannot unpack address>";
        return false;
    }
    os << wc << ":" << addr.to_hex();
    return true;
}

bool unpack_message(std::ostream& os, Ref<vm::Cell> msg, int mode) {
    if (msg.is_null()) {
        os << "<message not found>";
        return true;
    }
    vm::CellSlice cs{vm::NoVmOrd(), msg};
    block::gen::CommonMsgInfo info;
    Ref<vm::CellSlice> src, dest;
    switch (block::gen::t_CommonMsgInfo.get_tag(cs)) {
        case block::gen::CommonMsgInfo::ext_in_msg_info: {
            block::gen::CommonMsgInfo::Record_ext_in_msg_info info;
            if (!tlb::unpack(cs, info)) {
                LOG(DEBUG) << "cannot unpack inbound external message";
                return false;
            }
            os << "EXT-IN-MSG";
            if (!(mode & 2)) {
                os << " TO: ";
                if (!unpack_addr(os, std::move(info.dest))) {
                    return false;
                }
            }
            return true;
        }
        case block::gen::CommonMsgInfo::ext_out_msg_info: {
            block::gen::CommonMsgInfo::Record_ext_out_msg_info info;
            if (!tlb::unpack(cs, info)) {
                LOG(DEBUG) << "cannot unpack outbound external message";
                return false;
            }
            os << "EXT-OUT-MSG";
            if (!(mode & 1)) {
                os << " FROM: ";
                if (!unpack_addr(os, std::move(info.src))) {
                    return false;
                }
            }
            os << " LT:" << info.created_lt << " UTIME:" << info.created_at;
            return true;
        }
        case block::gen::CommonMsgInfo::int_msg_info: {
            block::gen::CommonMsgInfo::Record_int_msg_info info;
            if (!tlb::unpack(cs, info)) {
                LOG(DEBUG) << "cannot unpack internal message";
                return false;
            }
            os << "INT-MSG";
            if (!(mode & 1)) {
                os << " FROM: ";
                if (!unpack_addr(os, std::move(info.src))) {
                    return false;
                }
            }
            if (!(mode & 2)) {
                os << " TO: ";
                if (!unpack_addr(os, std::move(info.dest))) {
                    return false;
                }
            }
            os << " LT:" << info.created_lt << " UTIME:" << info.created_at;
            td::RefInt256 value;
            Ref<vm::Cell> extra;
            if (!block::unpack_CurrencyCollection(info.value, value, extra)) {
                LOG(ERROR) << "cannot unpack message value";
                return false;
            }
            os << " VALUE:" << value;
            if (extra.not_null()) {
                os << "+extra";
            }
            return true;
        }
        default:
            LOG(ERROR) << "cannot unpack message";
            return false;
    }
}


std::string message_info_str(Ref<vm::Cell> msg, int mode) {
    std::ostringstream os;
    if (!unpack_message(os, msg, mode)) {
        return "<cannot unpack message>";
    } else {
        return os.str();
    }
}

td::Result<td::UInt256> get_uint256(std::string str) {
    if (str.size() != 64) {
        return td::Status::Error("uint256 must have 64 bytes");
    }
    td::UInt256 res;
    for (size_t i = 0; i < 32; i++) {
        res.raw[i] = static_cast<td::uint8>(td::hex_to_int(str[2 * i]) * 16 + td::hex_to_int(str[2 * i + 1]));
    }
    return res;
}



void run_updater(td::actor::Scheduler* scheduler, td::actor::ActorOwn<TestNode>* owner){
  unsigned int microseconds = 2000000;
  while(true){
    usleep(microseconds);
    scheduler -> run_in_context([&] {
      td::actor::send_closure(owner -> get(), &TestNode::web_last);
    });
  }
}

void set_options(td::OptionsParser& p){
    p.set_description("Test Lite Client for TON Blockchain");

    p.add_option('h', "help", "prints_help", [&]() {
        char b[10240];
        td::StringBuilder sb(td::MutableSlice{b, 10000});
        sb << p;
        std::cout << sb.as_cslice().c_str();
        std::exit(2);
        return td::Status::OK();
    });

    p.add_option('C', "global-config", "file to read global config", [&](td::Slice fname) {
        td::actor::send_closure(x, &TestNode::set_global_config, fname.str());
        return td::Status::OK();
    });
    p.add_option('c', "local-config", "file to read local config", [&](td::Slice fname) {
        td::actor::send_closure(x, &TestNode::set_local_config, fname.str());
        return td::Status::OK();
    });
    p.add_option('r', "disable-readline", "", [&]() {
        td::actor::send_closure(x, &TestNode::set_readline_enabled, false);
        return td::Status::OK();
    });
    p.add_option('R', "enable-readline", "", [&]() {
        td::actor::send_closure(x, &TestNode::set_readline_enabled, true);
        return td::Status::OK();
    });
    p.add_option('D', "db", "root for dbs", [&](td::Slice fname) {
        td::actor::send_closure(x, &TestNode::set_db_root, fname.str());
        return td::Status::OK();
    });
    p.add_option('v', "verbosity", "set verbosity level", [&](td::Slice arg) {
        verbosity = td::to_integer<int>(arg);
        SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + verbosity);
        return (verbosity >= 0 && verbosity <= 9) ? td::Status::OK() : td::Status::Error("verbosity must be 0..9");
    });
    p.add_option('i', "idx", "set liteserver idx", [&](td::Slice arg) {
        auto idx = td::to_integer<int>(arg);
        td::actor::send_closure(x, &TestNode::set_liteserver_idx, idx);
        return td::Status::OK();
    });
    p.add_option('d', "daemonize", "set SIGHUP", [&]() {
        td::set_signal_handler(td::SignalType::HangUp,
                               [](int sig) {
#if TD_DARWIN || TD_LINUX
                                   close(0);
                             setsid();
#endif
                               })
                .ensure();
        return td::Status::OK();
    });
#if TD_DARWIN || TD_LINUX
    p.add_option('l', "logname", "log to file", [&](td::Slice fname) {
    auto FileLog = td::FileFd::open(td::CSlice(fname.str().c_str()),
                                    td::FileFd::Flags::Create | td::FileFd::Flags::Append | td::FileFd::Flags::Write)
                       .move_as_ok();

    dup2(FileLog.get_native_fd().fd(), 1);
    dup2(FileLog.get_native_fd().fd(), 2);
    return td::Status::OK();
  });
#endif
}




int main(int argc, char* argv[]) {
  SET_VERBOSITY_LEVEL(verbosity_INFO);
  td::set_default_failure_signal_handler();

  td::actor::ActorOwn<TestNode> node;

  td::OptionsParser p;

  set_options(p);

  td::actor::Scheduler scheduler({2});

  scheduler.run_in_context([&] { node = td::actor::create_actor<TestNode>("testnode"); });

  scheduler.run_in_context([&] { p.run(argc, argv).ensure(); });
  scheduler.run_in_context([&] {
    td::actor::send_closure(node, &TestNode::run);
    // TMP disable release due to having an ability to call obj in another threads
    // TODO: do requests directly w/o using actors
    // x.release();
  });

  // web server thread
  std::thread webserver = std::thread(TestNode::run_web_server, &scheduler, &node);

  // updater thread called 'last' command
  std::thread updater = std::thread(run_updater, &scheduler, &node);

  scheduler.run();

  return 0;
}