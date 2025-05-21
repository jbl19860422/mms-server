/*
 * @Author: jbl19860422
 * @Date: 2023-09-16 10:32:17
 * @LastEditTime: 2023-10-02 15:44:43
 * @LastEditors: jbl19860422
 * @Description: 
 * @FilePath: \mms\mms\server\http\http_server_base.cpp
 * Copyright (c) 2023 by jbl19860422@gitee.com, All Rights Reserved. 
 */
#include "log/log.h"

#include <memory>
#include <fstream>
#include <boost/shared_ptr.hpp>

#include "base/utils/utils.h"
#include "http_server_base.hpp"
#include "http_server_session.hpp"
#include "ws_conn.hpp"
#include "config/config.h"

namespace mms {
HttpServerBase::HttpServerBase(ThreadWorker *w):TcpServer(this, w) {

}

HttpServerBase::~HttpServerBase() {

}

bool HttpServerBase::start(uint16_t port, const std::string & ip) {
    if (!register_route()) {
        return false;
    }

    set_socket_inactive_timeout_ms(Config::get_instance()->get_socket_inactive_timeout_ms());
    if (0 == start_listen(port, ip)) {
        return true;
    } 
    return false;
}

void HttpServerBase::stop() {
    stop_listen();
}

void HttpServerBase::on_socket_open(std::shared_ptr<SocketInterface> tcp_socket) {
    std::shared_ptr<HttpServerSession> s = std::make_shared<HttpServerSession>(this, tcp_socket);
    tcp_socket->set_session(s);
    s->service();
}

void HttpServerBase::on_socket_close(std::shared_ptr<SocketInterface> tcp_socket) {
    std::shared_ptr<HttpServerSession> s = std::static_pointer_cast<HttpServerSession>(tcp_socket->get_session());
    tcp_socket->clear_session();
    if (s) {
        s->close();
    }
}

bool HttpServerBase::register_route() {
    return true;
}

bool HttpServerBase::on_get(const std::string & path, const HTTP_HANDLER & handler) {
    return get_route_tree_.add_route(path, handler);
}

bool HttpServerBase::on_static_fs(const std::string & path, const std::string & root_path) {
    if (!boost::ends_with(path, "/*")) {
        CORE_ERROR("create static fs failed, path:{} is not end with *", path);
        return false;
    }

    std::string prefix = path.substr(0, path.size() - 1);
    return get_route_tree_.add_route(path, std::bind(&HttpServerBase::static_fs_handler, this, root_path, prefix, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

bool HttpServerBase::on_post(const std::string & path, const HTTP_HANDLER & handler) {
    return post_route_tree_.add_route(path, handler);
}

bool HttpServerBase::on_websocket(const std::string & path, const WS_HANDLER & handler) {
    return websocket_route_tree_.add_route(path, handler);
}

boost::asio::awaitable<void> HttpServerBase::static_fs_handler(std::string root_path, std::string prefix, std::shared_ptr<HttpServerSession> session, std::shared_ptr<HttpRequest> req, std::shared_ptr<HttpResponse> resp) {
    (void)resp;
    (void)session;
    CORE_INFO("get static file request, path:{}", prefix + req->get_path_param("*"));
    std::ifstream file;
    auto suffix = req->get_path_param("*");
    file.open(root_path + "/" + suffix);
    if (!file.is_open()) {
        resp->add_header("Connection", "close");
        co_await resp->write_header(404, "Not Found");
        resp->close();
        co_return;
    }

    std::string ext_name;
    auto pos = suffix.find_last_of(".");
    if (pos != std::string::npos) {
        ext_name = suffix.substr(pos);
    }

    if (!ext_name.empty()) {
        auto it_content_type = http_content_type_map.find(ext_name);
        if (it_content_type == http_content_type_map.end()) {
            resp->add_header("Connection", "close");
            co_await resp->write_header(403, "Forbidden");
            resp->close();
            co_return;
        }
        resp->add_header("Content-Type", it_content_type->second);
    }
    
    resp->add_header("Connection", "close");
    file.seekg(0, std::ios::end);
    int32_t length = file.tellg();
    file.seekg(0, std::ios::beg);
    std::unique_ptr<char[]> body = std::make_unique<char[]>(length);
    file.read(body.get(), length);
    resp->add_header("Content-Length", std::to_string(length));
    if (!(co_await resp->write_header(200, "OK"))) {
        resp->close();
        co_return;
    }

    bool ret = co_await resp->write_data((uint8_t*)body.get(), length);
    if (!ret) {
        resp->close();
        co_return;
    }
    resp->close();
    co_return;
}

boost::asio::awaitable<bool> HttpServerBase::on_new_request(std::shared_ptr<HttpServerSession> session, std::shared_ptr<HttpRequest> req, std::shared_ptr<HttpResponse> resp) {
    CORE_DEBUG("get new http request, path:{}", req->get_path());
    switch(req->get_method()) {
        case GET : {
            if (is_websocket_req(req)) {
                auto handler = websocket_route_tree_.get_route(req->get_path(), req->path_params());
                if (!handler.has_value()) {
                    resp->add_header("Connection", "close");
                    co_await resp->write_header(404, "Not Found");
                    resp->close();
                } else {
                    // 返回websocket的http响应
                    resp->add_header("Upgrade", "websocket");
                    resp->add_header("Connection", "Upgrade");
                    std::string sec_websocket_accept;
                    auto k = req->get_header("Sec-WebSocket-Key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                    auto sha1 = Utils::sha1(k);
                    std::string base64;
                    Utils::encode_base64(sha1, base64);
                    resp->add_header("Sec-WebSocket-Accept", base64);
                    auto protocol = req->get_header("Sec-WebSocket-Protocol");
                    if (!protocol.empty()) {
                        resp->add_header("Sec-WebSocket-Protocol", protocol);
                    }
                    
                    if (!co_await resp->write_header(101, "Switching Protocols")) {
                        resp->close();
                        co_return false;
                    }
                    CORE_INFO("send upgrade response");
                    std::shared_ptr<WsConn> ws_conn = std::make_shared<WsConn>(resp->get_conn());
                    ws_conn->setReq(req);
                    co_await handler.value()(session, req, ws_conn);
                }
            } else {
                auto handler = get_route_tree_.get_route(req->get_path(), req->path_params());
                if (!handler.has_value()) {//404
                    resp->add_header("Connection", "close");
                    co_await resp->write_header(404, "Not Found");
                    resp->close();
                } else {
                    co_await handler.value()(session, req, resp);
                }
            }
            break;
        }
        case POST : {
            auto handler = post_route_tree_.get_route(req->get_path(), req->path_params());
            if (!handler.has_value()) {//404
                resp->add_header("Connection", "close");
                co_await resp->write_header(404, "Not Found");
                resp->close();
            } else {
                co_await handler.value()(session, req, resp);
            }
            break;
        }
        default : {
            break;
        }
    }
    co_return true;
}

bool HttpServerBase::is_websocket_req(std::shared_ptr<HttpRequest> req) {
    auto & host = req->get_header("Host");
    if (host.empty()) {
        return false;
    }

    auto & upgrade = req->get_header("Upgrade");
    if (upgrade != "websocket") {
        return false;
    }

    auto & connection = req->get_header("Connection");
    if (connection != "Upgrade") {
        return false;
    }

    auto & sec_websocket_key = req->get_header("Sec-WebSocket-Key");
    if (sec_websocket_key.empty()) {
        return false;
    }

    auto & sec_websocket_version = req->get_header("Sec-WebSocket-Version");
    if (sec_websocket_version.empty()) {
        return false;
    }

    try {
        auto isec_websocket_version = std::atoi(sec_websocket_version.c_str());
        if (isec_websocket_version != 13) {
            return false;
        }
    } catch (std::exception & e) {
        return false;
    }

    // auto & sec_websocket_protocol = req->get_header("Sec-WebSocket-Protocol");
    // if (sec_websocket_protocol.empty()) {
    //     return false;
    // }

    return true;
}

};