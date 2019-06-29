#include <iostream>
#include <sstream>

#if TD_DARWIN || TD_LINUX
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include <webserver/test_node.hpp>


int verbosity;

void run_updater(td::actor::Scheduler* scheduler, td::actor::ActorOwn<TestNode>* owner){
  unsigned int microseconds = 2000000;
  while(true){
    usleep(microseconds);
    scheduler -> run_in_context([&] {
      td::actor::send_closure(owner -> get(), &TestNode::web_last);
    });
  }
}

void set_options(td::OptionsParser& p, td::actor::ActorOwn<TestNode>* node){

}




int main(int argc, char* argv[]) {
  SET_VERBOSITY_LEVEL(verbosity_INFO);
  td::set_default_failure_signal_handler();

  td::actor::ActorOwn<TestNode> node;

  td::OptionsParser p;
  td::actor::Scheduler scheduler({2});

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
        td::actor::send_closure(node, &TestNode::set_global_config, fname.str());
        return td::Status::OK();
    });
    p.add_option('c', "local-config", "file to read local config", [&](td::Slice fname) {
        td::actor::send_closure(node, &TestNode::set_local_config, fname.str());
        return td::Status::OK();
    });

    p.add_option('r', "disable-readline", "", [&]() {
        td::actor::send_closure(node, &TestNode::set_readline_enabled, false);
        return td::Status::OK();
    });
    p.add_option('R', "enable-readline", "", [&]() {
        td::actor::send_closure(node, &TestNode::set_readline_enabled, true);
        return td::Status::OK();
    });
    p.add_option('D', "db", "root for dbs", [&](td::Slice fname) {
        td::actor::send_closure(node, &TestNode::set_db_root, fname.str());
        return td::Status::OK();
    });
    p.add_option('v', "verbosity", "set verbosity level", [&](td::Slice arg) {
        verbosity = td::to_integer<int>(arg);
        SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + verbosity);
        return (verbosity >= 0 && verbosity <= 9) ? td::Status::OK() : td::Status::Error("verbosity must be 0..9");
    });
    p.add_option('i', "idx", "set liteserver idx", [&](td::Slice arg) {
        auto idx = td::to_integer<int>(arg);
        td::actor::send_closure(node, &TestNode::set_liteserver_idx, idx);
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

  try{
     // set_options(p,&node);
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
  }
  catch (...){
      std::cerr<< "Some exception was thrown" <<std::endl;
  }
  return 0;
}