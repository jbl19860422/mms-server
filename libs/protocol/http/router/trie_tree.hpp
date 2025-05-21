#pragma once
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <string_view>
#include <boost/algorithm/string.hpp>

namespace mms {
template<typename HANDLER>
class TrieTree {
public:
    class TrieNode {
    public:
        char c;
        std::unordered_map<char, std::shared_ptr<TrieNode>> childrens;
        std::unordered_map<std::string, std::shared_ptr<TrieNode>> pchildrens;
        std::shared_ptr<TrieNode> get_child(char c) {
            auto it = childrens.find(c);
            if (it == childrens.end()) {
                return nullptr;
            }
            return it->second;
        }

        std::shared_ptr<TrieNode> get_param_child(const std::string & param_name) {
            auto it = pchildrens.find(param_name);
            if (it != pchildrens.end()) {
                return it->second;
            }
            return nullptr;
        }

        std::optional<HANDLER> handler_;
    };
    TrieTree() {
        root_.c = '/';
    }
    virtual ~TrieTree() {
    }
public:
    // 添加一条路由，成功返回true，失败返回false
    bool add_route(const std::string & path, const HANDLER & handler) {
        if (path.size() <= 0) {
            return false;
        }

        if (path[0] != root_.c) {
            return false;
        }

        return add_route(std::string_view(path.data() + 1, path.size() - 1), root_, handler);
    }
    // 获取一条路由
    std::optional<HANDLER> get_route(const std::string & path, std::unordered_map<std::string, std::string> & params) {
        if (path.size() <= 0) {
            return std::nullopt;
        }

        if (path[0] != root_.c) {
            return std::nullopt;
        }

        return get_route(std::string_view(path.data() + 1, path.size() - 1), root_, params);
    }
private:
    // 添加一条从node开始的路由
    bool add_route(const std::string_view & path, TrieNode & node, const HANDLER & handler) {
        // 递归结束条件
        if (path.size() <= 0) {
            if (node.handler_) {
                return false;//已经存在了
            }
            node.handler_ = handler;
            return true;
        }

        char c = path[0];//:stream
        if (c == ':') {//如果是冒号，则匹配到/,.或者末尾为止
            auto child_node = node.get_child(':');
            if (!child_node) {
                child_node = std::make_shared<TrieNode>();
                child_node->c = ':';
                node.childrens[c] = child_node;
            }

            size_t j = 0;//:stream.flv
            while (j < path.size() && path[j] != '/') {
                j++;
            }

            if (j == path.size()) {//到了末尾 /:stream
                std::string param_name(path.substr(1, j));
                auto param_child_node = child_node->get_param_child(param_name);
                if (!param_child_node) {
                    param_child_node = std::make_shared<TrieNode>();
                    child_node->pchildrens[param_name] = param_child_node;
                }

                param_child_node->handler_ = handler;
                return true;
            } else {
                std::string param_name(path.substr(1, j-1));//去掉'/','.'
                auto param_child_node = child_node->get_param_child(param_name);
                if (!param_child_node) {
                    param_child_node = std::make_shared<TrieNode>();
                }
                ///:app/get_streams    /get_streams 
                bool ret = add_route(std::string_view(path.data() + j, path.size() - j), *param_child_node, handler);
                if (!ret) {
                    return false;
                }
                child_node->pchildrens[param_name] = param_child_node;
                return true;
            }
        } else if (c == '*') {//static/imgs/*
            auto child_node = node.get_child('*');
            if (child_node) {//已经存在了，不应该出现
                return false;
            }
            child_node = std::make_shared<TrieNode>();
            child_node->c = '*';
            child_node->handler_ = handler;
            node.childrens['*'] = child_node;
            return true;
        } else {//a,b,f,
            auto child_node = node.get_child(c);
            if (child_node) {//存在
                return add_route(std::string_view(path.data() + 1, path.size() - 1), *child_node, handler);
            } else {//不存在
                child_node = std::make_shared<TrieNode>();
                child_node->c = c;
                bool ret = add_route(std::string_view(path.data() + 1, path.size() - 1), *child_node, handler);
                if (!ret) {
                    return false;
                }
                
                node.childrens[c] = child_node;
                return true;
            }
        }
    }
    // 获取一条从node开始的路由
    std::optional<HANDLER> get_route(const std::string_view & path, 
                                     TrieNode & node, 
                                     std::unordered_map<std::string, std::string> & params) {
        if (path.size() <= 0) {
            return node.handler_;
        }
        
        std::shared_ptr<TrieNode> child;
        std::optional<HANDLER> handler;
        child = node.get_child(path[0]);
        if (child) {
            handler = get_route(std::string_view(path.data() + 1, path.size() - 1), *child, params);
        }

        if (handler) {
            return handler;
        }

        child = node.get_child(':');//   /apd  /:app
        if (child) {
            size_t j = 0;  // app/   test.flv
            while (j < path.size() && path[j] != '/') {
                j++;
            }
            std::string param_val(path.substr(0, j));
            for (auto it = child->pchildrens.begin(); it != child->pchildrens.end(); it++) {
                handler = get_route(std::string_view(path.data() + j, path.size() - j), *(it->second), params);
                if (handler) {
                    // it->first = param_name
                    params[it->first] = param_val;
                    break;
                }
            } 
        }

        if (handler) {
            return handler;
        }

        child = node.get_child('*');
        if (child) {
            params["*"] = path.substr(0);
            handler = child->handler_;
        }
        return handler;
    }
private:
    TrieNode root_;
};

};