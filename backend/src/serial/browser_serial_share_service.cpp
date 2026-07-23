#include "serial/browser_serial_share_service.hpp"

#include "logging/logger.hpp"

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

nlohmann::json BrowserSerialShareService::ListShares()
{
    auto result = nlohmann::json::array();
    std::lock_guard lock(mutex_);
    for (auto iterator = shares_.begin(); iterator != shares_.end();)
    {
        const auto owner = iterator->second.owner.lock();
        if (!owner || !owner->connected())
        {
            for (const auto& [subscription_id, ignored] : iterator->second.subscribers)
                subscriptions_.erase(subscription_id);
            iterator = shares_.erase(iterator);
            continue;
        }
        result.push_back({{"id", iterator->second.id},
                          {"name", iterator->second.name},
                          {"portLabel", iterator->second.port_label},
                          {"writeEnabled", iterator->second.write_enabled},
                          {"viewers", iterator->second.subscribers.size()}});
        ++iterator;
    }
    return {{"shares", std::move(result)}};
}

void BrowserSerialShareService::Publish(const std::string& share_id,
                                        const std::string& name,
                                        const std::string& port_label,
                                        bool write_enabled,
                                        const drogon::WebSocketConnectionPtr& owner)
{
    if (!ValidId(share_id)) throw std::runtime_error("共享标识无效。");
    if (name.empty() || name.size() > 120) throw std::runtime_error("共享名称不能为空且不能超过 120 个字符。");
    std::lock_guard lock(mutex_);
    const auto found = shares_.find(share_id);
    if (found != shares_.end())
    {
        const auto current_owner = found->second.owner.lock();
        if (current_owner && current_owner->connected() && current_owner != owner)
            throw std::runtime_error("该浏览器串口共享标识已经被占用。");
    }
    Share share;
    share.id = share_id;
    share.name = name;
    share.port_label = port_label.substr(0, 200);
    share.write_enabled = write_enabled;
    share.owner = owner;
    shares_[share_id] = std::move(share);
    const auto log_message = "Browser serial share published: " + share_id;
    logI(log_message.c_str());
}

void BrowserSerialShareService::Update(const std::string& share_id,
                                       const std::string& name,
                                       bool write_enabled,
                                       const drogon::WebSocketConnectionPtr& owner)
{
    if (name.empty() || name.size() > 120) throw std::runtime_error("共享名称不能为空且不能超过 120 个字符。");
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end() || found->second.owner.lock() != owner)
            throw std::runtime_error("当前连接不是该共享串口的拥有者。");
        found->second.name = name;
        found->second.write_enabled = write_enabled;
    }
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
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end() || found->second.owner.lock() != owner)
            throw std::runtime_error("当前连接不是该共享串口的拥有者。");
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
    for (const auto& subscriber : subscribers) subscriber->send(data, drogon::WebSocketMessageType::Binary);
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
    {
        std::lock_guard lock(mutex_);
        const auto found = shares_.find(share_id);
        if (found == shares_.end() || found->second.owner.lock() != owner) return;
        for (const auto& [subscription_id, weak_connection] : found->second.subscribers)
        {
            subscriptions_.erase(subscription_id);
            if (const auto connection = weak_connection.lock()) subscribers.push_back(connection);
        }
        shares_.erase(found);
    }
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
                 {"writeEnabled", found->second.write_enabled}};
    }
    const auto message = state.dump();
    for (const auto& connection : connections) connection->send(message);
}
} // namespace spacestation::serial
