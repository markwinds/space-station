#include "serial/browser_serial_share_service.hpp"

#include "logging/logger.hpp"
#include "plugins/terminal_plugin_service.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <vector>

namespace spacestation::serial
{
namespace
{
constexpr std::size_t kMaximumBacklogBytes = 512 * 1024;
constexpr std::size_t kMaximumMessageBytes = 1024 * 1024;

std::string NextSubscriptionId()
{
    static std::atomic<std::uint64_t> counter{0};
    return "browser-share-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(++counter);
}

bool ValidId(const std::string& value)
{
    return !value.empty() && value.size() <= 128;
}
} // namespace

BrowserSerialShareService::BrowserSerialShareService(plugins::TerminalPluginService* plugin_service)
    : plugin_service_(plugin_service)
{
}

BrowserSerialShareService::~BrowserSerialShareService()
{
    if (!plugin_service_) return;
    std::vector<std::string> plugin_sessions;
    {
        std::lock_guard lock(mutex_);
        for (const auto& [ignored, share] : shares_)
            if (!share.plugin_session_id.empty()) plugin_sessions.push_back(share.plugin_session_id);
    }
    for (const auto& session_id : plugin_sessions) plugin_service_->UnregisterSession(session_id);
}

nlohmann::json BrowserSerialShareService::ListShares()
{
    auto result = nlohmann::json::array();
    std::vector<std::string> stale_plugin_sessions;
    {
        std::lock_guard lock(mutex_);
        for (auto iterator = shares_.begin(); iterator != shares_.end();)
        {
            const auto owner = iterator->second.owner.lock();
            if (!owner || !owner->connected())
            {
                for (const auto& [subscription_id, ignored] : iterator->second.subscribers)
                    subscriptions_.erase(subscription_id);
                if (!iterator->second.plugin_session_id.empty())
                    stale_plugin_sessions.push_back(iterator->second.plugin_session_id);
                iterator = shares_.erase(iterator);
                continue;
            }
            if (!iterator->second.discoverable)
            {
                ++iterator;
                continue;
            }
            result.push_back({{"id", iterator->second.id},
                              {"name", iterator->second.name},
                              {"portLabel", iterator->second.port_label},
                              {"pluginTarget", iterator->second.plugin_target},
                              {"writeEnabled", iterator->second.write_enabled},
                              {"viewers", iterator->second.subscribers.size()}});
            ++iterator;
        }
    }
    if (plugin_service_)
        for (const auto& session_id : stale_plugin_sessions) plugin_service_->UnregisterSession(session_id);
    return {{"shares", std::move(result)}};
}

void BrowserSerialShareService::Publish(const std::string& share_id,
                                        const std::string& name,
                                        const std::string& port_label,
                                        bool write_enabled,
                                        bool discoverable,
                                        const std::string& plugin_target,
                                        const drogon::WebSocketConnectionPtr& owner)
{
    if (!ValidId(share_id)) throw std::runtime_error("共享标识无效。");
    if (name.empty() || name.size() > 120) throw std::runtime_error("共享名称不能为空且不能超过 120 个字符。");
    std::string replaced_plugin_session;
    std::string plugin_session_id;
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found != shares_.end())
        {
            const auto current_owner = found->second.owner.lock();
            if (current_owner && current_owner->connected() && current_owner != owner)
                throw std::runtime_error("该浏览器串口共享标识已经被占用。");
            replaced_plugin_session = found->second.plugin_session_id;
        }
        Share share;
        share.id = share_id;
        share.name = name;
        share.port_label = port_label.substr(0, 200);
        share.write_enabled = write_enabled;
        share.discoverable = discoverable;
        share.plugin_target = ValidId(plugin_target) ? plugin_target : share_id;
        share.owner = owner;
        if (plugin_service_)
        {
            plugin_session_id = NextSubscriptionId();
            share.plugin_session_id = plugin_session_id;
        }
        shares_[share_id] = std::move(share);
    }
    if (plugin_service_)
    {
        if (!replaced_plugin_session.empty()) plugin_service_->UnregisterSession(replaced_plugin_session);
        plugin_service_->RegisterSession(
            plugin_session_id, "browser-serial", ValidId(plugin_target) ? plugin_target : share_id,
            [this, share_id](std::string data) { PluginWrite(share_id, std::move(data)); });
    }
    const auto log_message = std::string("Browser serial bridge published: ") + share_id +
                             (discoverable ? " (discoverable)" : " (private)");
    logI(log_message.c_str());
}

void BrowserSerialShareService::Update(const std::string& share_id,
                                       const std::string& name,
                                       bool write_enabled,
                                       bool discoverable,
                                       const drogon::WebSocketConnectionPtr& owner)
{
    if (name.empty() || name.size() > 120) throw std::runtime_error("共享名称不能为空且不能超过 120 个字符。");
    std::vector<drogon::WebSocketConnectionPtr> disconnected_subscribers;
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end() || found->second.owner.lock() != owner)
            throw std::runtime_error("当前连接不是该共享串口的拥有者。");
        found->second.name = name;
        found->second.write_enabled = write_enabled;
        if (found->second.discoverable && !discoverable)
        {
            for (const auto& [subscription_id, weak_connection] : found->second.subscribers)
            {
                subscriptions_.erase(subscription_id);
                if (const auto connection = weak_connection.lock()) disconnected_subscribers.push_back(connection);
            }
            found->second.subscribers.clear();
            found->second.backlog.clear();
        }
        found->second.discoverable = discoverable;
    }
    const auto unavailable = nlohmann::json(
        {{"type", "unavailable"}, {"message", "浏览器串口拥有者已经停止共享。"}}).dump();
    for (const auto& subscriber : disconnected_subscribers) subscriber->send(unavailable);
    BroadcastState(share_id);
}

std::string BrowserSerialShareService::Subscribe(const std::string& share_id,
                                                  const drogon::WebSocketConnectionPtr& connection)
{
    std::string backlog;
    std::string subscription_id;
    nlohmann::json info;
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end()) throw std::runtime_error("浏览器串口共享不存在或已经离线。");
        if (!found->second.discoverable) throw std::runtime_error("浏览器串口未共享给其他客户端。");
        const auto owner = found->second.owner.lock();
        if (!owner || !owner->connected()) throw std::runtime_error("浏览器串口拥有者已经离线。");
        subscription_id = NextSubscriptionId();
        found->second.subscribers[subscription_id] = connection;
        subscriptions_[subscription_id] = share_id;
        backlog = found->second.backlog;
        info = {{"type", "subscribed"},
                {"shareId", share_id},
                {"name", found->second.name},
                {"writeEnabled", found->second.write_enabled},
                {"viewers", found->second.subscribers.size()}};
    }
    connection->send(info.dump());
    if (!backlog.empty()) connection->send(backlog, drogon::WebSocketMessageType::Binary);
    BroadcastState(share_id);
    return subscription_id;
}

void BrowserSerialShareService::OwnerData(const std::string& share_id,
                                          const drogon::WebSocketConnectionPtr& owner,
                                          std::string data)
{
    if (data.empty()) return;
    if (data.size() > kMaximumMessageBytes) throw std::runtime_error("单次共享串口数据不能超过 1 MiB。");
    std::vector<drogon::WebSocketConnectionPtr> subscribers;
    std::string plugin_session_id;
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end() || found->second.owner.lock() != owner)
            throw std::runtime_error("当前连接不是该共享串口的拥有者。");
        plugin_session_id = found->second.plugin_session_id;
        if (found->second.discoverable)
        {
            found->second.backlog.append(data);
            if (found->second.backlog.size() > kMaximumBacklogBytes)
                found->second.backlog.erase(0, found->second.backlog.size() - kMaximumBacklogBytes);
            for (auto iterator = found->second.subscribers.begin(); iterator != found->second.subscribers.end();)
            {
                if (const auto connection = iterator->second.lock(); connection && connection->connected())
                {
                    subscribers.push_back(connection);
                    ++iterator;
                }
                else
                {
                    subscriptions_.erase(iterator->first);
                    iterator = found->second.subscribers.erase(iterator);
                }
            }
        }
    }
    if (plugin_service_ && !plugin_session_id.empty()) plugin_service_->OnOutput(plugin_session_id, data);
    for (const auto& subscriber : subscribers) subscriber->send(data, drogon::WebSocketMessageType::Binary);
}

void BrowserSerialShareService::PluginWrite(const std::string& share_id, std::string data)
{
    if (data.empty()) return;
    if (data.size() > kMaximumMessageBytes) throw std::runtime_error("插件单次串口发送不能超过 1 MiB。");
    drogon::WebSocketConnectionPtr owner;
    {
        std::lock_guard lock(mutex_);
        const auto share = shares_.find(share_id);
        if (share == shares_.end()) throw std::runtime_error("浏览器串口共享已经离线。");
        owner = share->second.owner.lock();
    }
    if (!owner || !owner->connected()) throw std::runtime_error("浏览器串口拥有者已经离线。");
    owner->send(std::move(data), drogon::WebSocketMessageType::Binary);
}

void BrowserSerialShareService::SubscriberWrite(const std::string& subscription_id, std::string data)
{
    if (data.empty()) return;
    if (data.size() > kMaximumMessageBytes) throw std::runtime_error("单次共享串口发送不能超过 1 MiB。");
    drogon::WebSocketConnectionPtr owner;
    {
        std::lock_guard lock(mutex_);
        const auto subscription = subscriptions_.find(subscription_id);
        if (subscription == subscriptions_.end()) throw std::runtime_error("共享串口订阅尚未建立。");
        const auto share = shares_.find(subscription->second);
        if (share == shares_.end()) throw std::runtime_error("共享串口已经离线。");
        if (!share->second.discoverable) throw std::runtime_error("浏览器串口已经停止共享。");
        if (!share->second.write_enabled) throw std::runtime_error("共享串口为只读，拥有者未允许远程写入。");
        owner = share->second.owner.lock();
    }
    if (!owner || !owner->connected()) throw std::runtime_error("浏览器串口拥有者已经离线。");
    owner->send(std::move(data), drogon::WebSocketMessageType::Binary);
}

void BrowserSerialShareService::Unpublish(const std::string& share_id,
                                          const drogon::WebSocketConnectionPtr& owner)
{
    std::vector<drogon::WebSocketConnectionPtr> subscribers;
    std::string plugin_session_id;
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end() || found->second.owner.lock() != owner) return;
        plugin_session_id = found->second.plugin_session_id;
        for (const auto& [subscription_id, weak_connection] : found->second.subscribers)
        {
            subscriptions_.erase(subscription_id);
            if (const auto connection = weak_connection.lock()) subscribers.push_back(connection);
        }
        shares_.erase(found);
    }
    if (plugin_service_ && !plugin_session_id.empty()) plugin_service_->UnregisterSession(plugin_session_id);
    const auto event = nlohmann::json({{"type", "unavailable"}, {"message", "浏览器串口拥有者已经停止共享。"}}).dump();
    for (const auto& subscriber : subscribers) subscriber->send(event);
}

void BrowserSerialShareService::Detach(const std::string& subscription_id)
{
    std::string share_id;
    {
        std::lock_guard lock(mutex_);
        const auto subscription = subscriptions_.find(subscription_id);
        if (subscription == subscriptions_.end()) return;
        share_id = subscription->second;
        if (const auto share = shares_.find(share_id); share != shares_.end())
            share->second.subscribers.erase(subscription_id);
        subscriptions_.erase(subscription);
    }
    BroadcastState(share_id);
}

void BrowserSerialShareService::BroadcastState(const std::string& share_id)
{
    std::vector<drogon::WebSocketConnectionPtr> connections;
    nlohmann::json state;
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end()) return;
        if (const auto owner = found->second.owner.lock(); owner && owner->connected()) connections.push_back(owner);
        for (const auto& [ignored, weak_connection] : found->second.subscribers)
            if (const auto connection = weak_connection.lock(); connection && connection->connected()) connections.push_back(connection);
        state = {{"type", "share-state"}, {"shareId", share_id}, {"viewers", found->second.subscribers.size()},
                 {"writeEnabled", found->second.write_enabled}, {"discoverable", found->second.discoverable}};
    }
    const auto message = state.dump();
    for (const auto& connection : connections) connection->send(message);
}
} // namespace spacestation::serial
