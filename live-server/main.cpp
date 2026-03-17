#include <limits.h>
#include <sys/resource.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>
#include <iostream>

#include "base/thread/thread_pool.hpp"
#include "base/utils/utils.h"
#include "config/config.h"
#include "log/log.h"
#include "server/http/http_api_server.hpp"
#include "server/http/http_live_server.hpp"
#include "server/http/https_live_server.hpp"
#include "server/rtmp/rtmp_server.hpp"
#include "server/rtmp/rtmps_server.hpp"
#include "server/rtsp/rtsp_server.hpp"
#include "server/rtsp/rtsps_server.hpp"
#include "server/stun/stun_server.hpp"
#include "server/webrtc/webrtc_server.hpp"
#include "service/conn/http_conn_pools.h"
#include "service/dns/dns_service.hpp"
#include "version.h"
#include "recorder/recorder_db.h"
#include "system/system.h"

using namespace mms;

void wait_exit() {
    boost::asio::io_context io_ctx;
    boost::asio::signal_set sigset(io_ctx, SIGINT, SIGTERM);
    sigset.async_wait([&io_ctx](auto,auto) {
        io_ctx.stop();
    });
    io_ctx.run();
}

void init_log(bool is_debug_mode) {
    spdlog::set_level(spdlog::level::debug);
    Log::init(is_debug_mode);
    CORE_INFO("Welcome to mms!");
}

void system_setup() {
    struct rlimit rlim, rlim_new;
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        rlim_new.rlim_cur = rlim_new.rlim_max = RLIM_INFINITY;
        if (setrlimit(RLIMIT_NOFILE, &rlim_new) != 0) {
            rlim_new.rlim_cur = rlim_new.rlim_max = rlim.rlim_max;
            setrlimit(RLIMIT_NOFILE, &rlim_new);
        }
    }
}

int main(int argc, char *argv[]) {
    system_setup();
    boost::program_options::variables_map vm;
    boost::program_options::options_description opts("all options");
    try {
        opts.add_options()("version,v", "show the version")(
            "config,c", boost::program_options::value<std::string>(), "the config file path")(
            "help,h", "mms is a multi media server.")("debug,d", "output debug log to console.");

        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, opts), vm);
        boost::program_options::notify(vm);
    } catch (std::exception &exp) {
        spdlog::error("{}", exp.what());
        return -1;
    }

    bool is_debug_mode = false;
    if (vm.count("debug")) {
        is_debug_mode = true;
    } else if (vm.count("version")) {
        spdlog::info("mms-server version:{}", VERSION_STR);
        return 0;
    }

    init_log(is_debug_mode);
    if (vm.count("help")) {  // 若参数中有help选项
        std::cerr << opts << std::endl;
        return -1;
    } else {
        if (!vm.count("config")) {
            spdlog::error("please use -c option to set the config dir.");
            return -2;
        }
    }

    thread_pool_inst::get_mutable_instance().start(std::thread::hardware_concurrency());
    System::get_instance().init(thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
    HttpConnPools::get_instance().set_worker(thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
    
    std::string config_path = vm["config"].as<std::string>();
    auto config = Config::get_instance();
    try {
        if (!config->load_config(config_path)) {
            spdlog::error("failed load config:{}, program exit!", config_path);
            return -3;
        }
    } catch (std::exception &exp) {
        CORE_ERROR("load config failed, err:{}", exp.what());
        return -4;
    }

    if (config->get_log_level() == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (config->get_log_level() == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (config->get_log_level() == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (config->get_log_level() == "err") {
        spdlog::set_level(spdlog::level::err);
    } else if (config->get_log_level() == "warn") {
        spdlog::set_level(spdlog::level::warn);
    }

    if (!RecorderDb::get_instance().init()) {
        CORE_ERROR("init recorder db failed");
        return -1;
    }
    
    // start dns service
    DnsService &dns_service = DnsService::get_instance();
    dns_service.start();
    // start rtmp server
    std::shared_ptr<RtmpServer> rtmp_server;
    if (config->get_rtmp_config().is_enabled()) {
        rtmp_server =
            std::make_shared<RtmpServer>(thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
        if (!rtmp_server->start(config->get_rtmp_config().port())) {
            CORE_ERROR("start rtmp server on port:{} failed", config->get_rtmp_config().port());
            return -1;
        }
        CORE_INFO("start rtmp server on port:{} succeed", config->get_rtmp_config().port());
    }

    std::shared_ptr<RtmpsServer> rtmps_server;
    if (config->get_rtmps_config().is_enabled()) {
        rtmps_server =
            std::make_shared<RtmpsServer>(thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
        if (!rtmps_server->start(config->get_rtmps_config().port())) {
            CORE_ERROR("start rtmp server on port:{} failed", config->get_rtmps_config().port());
            return -1;
        }
        CORE_INFO("start rtmps server on port:{} succeed", config->get_rtmps_config().port());
    }

    std::shared_ptr<HttpLiveServer> http_live_server;
    if (config->get_http_config().is_enabled()) {
        http_live_server = std::make_shared<HttpLiveServer>(
            thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
        if (!http_live_server->start(config->get_http_config().port())) {
            CORE_ERROR("start http live server on port:{} failed", config->get_http_config().port());
            return -3;
        }
        CORE_INFO("start http live server on port:{} succeed", config->get_http_config().port());
    }

    std::shared_ptr<HttpsLiveServer> https_live_server;
    if (config->get_https_config().is_enabled()) {
        https_live_server = std::make_shared<HttpsLiveServer>(
            thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
        if (!https_live_server->start(config->get_https_config().port())) {
            CORE_ERROR("start https live server on port:{} failed", config->get_https_config().port());
            return -3;
        }
        CORE_INFO("start https live server on port:{} succeed", config->get_https_config().port());
    }

    std::shared_ptr<RtspServer> rtsp_server;
    if (config->get_rtsp_config().is_enabled()) {
        rtsp_server =
            std::make_shared<RtspServer>(thread_pool_inst::get_mutable_instance().get_all_workers());
        if (!rtsp_server->start(config->get_rtsp_config().port())) {
            CORE_ERROR("start rtsp server on port:{} failed", config->get_rtsp_config().port());
            return -3;
        }
        CORE_INFO("start rtsp server on port:{} succeed", config->get_rtsp_config().port());
    }

    std::shared_ptr<RtspsServer> rtsps_server;
    if (config->get_rtsps_config().is_enabled()) {
        rtsps_server =
            std::make_shared<RtspsServer>(thread_pool_inst::get_mutable_instance().get_all_workers());
        if (!rtsps_server->start(config->get_rtsps_config().port())) {
            CORE_ERROR("start rtsps server on port:{} failed", config->get_rtsps_config().port());
            return -3;
        }
        CORE_INFO("start rtsps server on port:{} succeed", config->get_rtsps_config().port());
    }

    auto &webrtc_config = config->get_webrtc_config();
    std::shared_ptr<WebRtcServer> webrtc_server;
    std::shared_ptr<StunServer> stun_server;

    if (webrtc_config.is_enabled()) {
        stun_server =
            std::make_shared<StunServer>(thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
        if (!stun_server->start(webrtc_config.get_ip())) {
            CORE_ERROR("start webrtc stun server failed");
            return -6;
        }

        CORE_INFO("start stun server on:{} ok", webrtc_config.get_ip());

        std::string listen_ip;
        if (!webrtc_config.get_internal_ip().empty()) {
            listen_ip = webrtc_config.get_internal_ip();
        } else {
            listen_ip = webrtc_config.get_ip();
        }

        webrtc_server =
            std::make_shared<WebRtcServer>(thread_pool_inst::get_mutable_instance().get_all_workers());
        if (!webrtc_server->start(listen_ip, webrtc_config.get_ip(), webrtc_config.get_udp_port())) {
            CORE_ERROR("start webrtc server failed");
            return -7;
        }
        CORE_INFO("start webrtc server succeed, listen ip:{}, udp port:{}", listen_ip,
                  webrtc_config.get_udp_port());
    }

    if (http_live_server && webrtc_server) {
        http_live_server->set_webrtc_server(webrtc_server);
    }

    if (https_live_server && webrtc_server) {
        https_live_server->set_webrtc_server(webrtc_server);
    }

    std::shared_ptr<HttpApiServer> http_api_server;
    if (config->get_http_api_config().is_enabled()) {
        http_api_server =
            std::make_shared<HttpApiServer>(thread_pool_inst::get_mutable_instance().get_worker(RAND_WORKER));
        if (!http_api_server->start(config->get_http_api_config().port())) {
            CORE_ERROR("start http api server on port:{} failed", config->get_http_api_config().port());
            return -3;
        }
        CORE_INFO("start http api server on port:{} succeed", config->get_http_api_config().port());
    }

    wait_exit();

    if (webrtc_server) {
        CORE_INFO("stop webrtc server...");
        webrtc_server->stop();
        CORE_INFO("stop webrtc server done");
    }

    if (stun_server) {
        CORE_INFO("stop webrtc stun server...");
        stun_server->stop();
        CORE_INFO("stop webrtc stun server done");
    }

    if (http_api_server) {
        CORE_INFO("stop http api server...");
        http_api_server->stop();
        CORE_INFO("stop http api server done");
    }

    if (rtsps_server) {
        CORE_INFO("stop rtsps server...");
        rtsps_server->stop();
        CORE_INFO("stop rtsps server done");
    }

    if (rtsp_server) {
        CORE_INFO("stop rtsp server...");
        rtsp_server->stop();
        CORE_INFO("stop rtsp server done");
    }

    if (rtmps_server) {
        CORE_INFO("stop rtmps live server...");
        rtmps_server->stop();
        CORE_INFO("stop rtmps live server done");
    }

    if (rtmp_server) {
        CORE_INFO("stop rtmp live server...");
        rtmp_server->stop();
        CORE_INFO("stop rtmp live server done");
    }

    if (https_live_server) {
        CORE_INFO("stop https live server...");
        https_live_server->stop();
        CORE_INFO("stop https live server done");
    }

    if (http_live_server) {
        CORE_INFO("stop http live server...");
        http_live_server->stop();
        CORE_INFO("stop http live server done");
    }

    DnsService::get_instance().stop();
    System::get_instance().uninit();
    thread_pool_inst::get_mutable_instance().stop();
    sleep(1);
    CORE_INFO("mms exit!!!");
    return 0;
}