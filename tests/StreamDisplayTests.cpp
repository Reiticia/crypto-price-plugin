#include "CryptoPricePlugin.h"
#include <chrono>
#include <iostream>
#include <string>

namespace cryptoprice
{
struct CryptoPricePluginTestAccess
{
    static int Run()
    {
        int failures{};
        const auto check = [&](bool ok, const char* text) {
            if (!ok) { ++failures; std::cerr << "FAILED: " << text << '\n'; }
        };
        CryptoPricePlugin plugin;
        Settings settings;
        settings.refreshSeconds = 300;
        settings.showChange = false;
        settings.instruments = {{L"SPOT", L"BTCUSDT", 2, DataSource::Bitget},
                                {L"SPOT", L"ETHUSDT", 2, DataSource::Bitget}};
        plugin.ApplySettings(settings);
        const auto generation = plugin.generation_.load();
        plugin.pending_ = {generation, {}};
        for (const auto& instrument : settings.instruments)
            plugin.pending_.quotes.push_back({instrument, false, {}, L"Waiting"});
        const auto price = [](std::size_t index, const char* last) {
            Ticker ticker;
            ticker.lastPrice = last;
            return StreamQuote{index, true, ticker, {}};
        };
        const auto value = [&](int index) { return std::wstring(plugin.GetItem(index + 1)->GetItemValueText()); };

        plugin.QueueStreamQuotes(generation, {price(0, "100"), {1, false, {}, L"Offline"}});
        plugin.ApplyPendingResult();
        check(value(0) == L"100.00" && value(1) == L"--", "initial full refresh");
        const auto deadline = plugin.nextRefresh_;

        plugin.QueueStreamQuotes(generation, {price(0, "200")});
        plugin.ApplyPendingResult();
        check(value(0) == L"100.00" && plugin.hasPending_, "existing failed item does not bypass price interval");
        plugin.QueueStreamQuotes(generation, {{1, false, {}, L"Offline"}});
        plugin.ApplyPendingResult();
        check(value(0) == L"100.00" && plugin.nextRefresh_ == deadline, "repeated error does not bypass or postpone interval");

        plugin.QueueStreamQuotes(generation, {{1, false, {}, L"Subscription failed"}});
        plugin.ApplyPendingResult();
        check(std::wstring(plugin.GetTooltipInfo()).find(L"Subscription failed") != std::wstring::npos,
              "changed error is shown immediately");
        check(value(0) == L"100.00" && plugin.hasPending_ && plugin.nextRefresh_ == deadline,
              "urgent error leaves unrelated prices pending and preserves deadline");
        plugin.ApplyPendingResult();
        check(value(0) == L"100.00", "consumed error does not remain urgent");

        plugin.nextRefresh_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        plugin.ApplyPendingResult();
        check(value(0) == L"200.00" && !plugin.hasPending_, "pending price is applied at deadline without another network frame");
        const auto nextDeadline = plugin.nextRefresh_;
        plugin.QueueStreamQuotes(generation, {{0, false, {}, L"Disconnected"}});
        plugin.ApplyPendingResult();
        check(value(0) == L"--" && plugin.nextRefresh_ == nextDeadline, "disconnect immediately invalidates cached price");
        plugin.QueueStreamQuotes(generation, {price(0, "300")});
        plugin.ApplyPendingResult();
        check(value(0) == L"--", "recovery prices remain throttled");

        plugin.QueueStreamQuotes(generation, {{0, false, {}, L"Temporary error"}, price(0, "400")});
        plugin.ApplyPendingResult();
        check(value(0) == L"--" && !plugin.pending_.quotes[0].urgentError,
              "recovery before UI consumption clears superseded urgent error");
        plugin.QueueStreamQuotes(generation - 1, {{0, false, {}, L"Stale generation"}});
        check(plugin.pending_.quotes[0].success && plugin.pending_.quotes[0].ticker.lastPrice == "400",
              "stale generation cannot change pending state");
        plugin.nextRefresh_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        plugin.ApplyPendingResult();
        check(value(0) == L"400.00", "latest coalesced price is eventually displayed");
        return failures ? 1 : 0;
    }
};
} // namespace cryptoprice

int main() { return cryptoprice::CryptoPricePluginTestAccess::Run(); }
