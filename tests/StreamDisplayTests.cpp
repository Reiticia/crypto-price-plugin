#include "CryptoPricePlugin.h"
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
        settings.showChange = false;
        settings.instruments = {{L"SPOT", L"BTCUSDT", 2, DataSource::Bitget},
                                {L"SPOT", L"ETHUSDT", 2, DataSource::Bitget}};
        plugin.ApplySettings(settings);
        const auto generation = plugin.generation_.load();
        plugin.pending_ = {generation, {}};
        for (const auto& instrument : settings.instruments)
            plugin.pending_.quotes.push_back({instrument, false, {}, L"Waiting"});
        // Exercise the real host callback without starting network workers.
        plugin.streamStarted_ = true;
        const auto price = [](std::size_t index, const char* last) {
            Ticker ticker;
            ticker.lastPrice = last;
            return StreamQuote{index, true, ticker, {}};
        };
        const auto value = [&](int index) { return std::wstring(plugin.GetItem(index + 1)->GetItemValueText()); };

        plugin.QueueStreamQuotes(generation, {price(0, "100"), {1, false, {}, L"Offline"}});
        check(value(0) == L"--" && plugin.hasPending_, "worker queues data without mutating host display strings");
        plugin.DataRequired();
        check(value(0) == L"100.00" && value(1) == L"--" && !plugin.hasPending_, "first host callback consumes pushed quotes");

        plugin.QueueStreamQuotes(generation, {price(0, "200")});
        plugin.DataRequired();
        check(value(0) == L"200.00" && !plugin.hasPending_, "consecutive price update has no display interval");
        plugin.QueueStreamQuotes(generation, {{1, false, {}, L"Offline"}, price(0, "210")});
        plugin.DataRequired();
        check(value(0) == L"210.00" && value(1) == L"--", "repeated error does not delay a healthy instrument");

        plugin.QueueStreamQuotes(generation, {{1, false, {}, L"Subscription failed"}, price(0, "220")});
        plugin.DataRequired();
        check(value(0) == L"220.00" && !plugin.hasPending_, "changed error and successful quote are consumed together");
        check(std::wstring(plugin.GetTooltipInfo()).find(L"Subscription failed") != std::wstring::npos,
              "changed error appears in tooltip");
        check(std::wstring(plugin.GetItem(0)->GetItemValueText()).find(L"220.00") != std::wstring::npos,
              "aggregate reflects latest pushed price");
        plugin.DataRequired();
        check(value(0) == L"220.00" && !plugin.hasPending_, "host refresh without a push preserves displayed quote");

        plugin.QueueStreamQuotes(generation, {{0, false, {}, L"Disconnected"}});
        plugin.DataRequired();
        check(value(0) == L"--", "disconnect clears cached price on next host callback");
        plugin.QueueStreamQuotes(generation, {price(0, "300")});
        plugin.DataRequired();
        check(value(0) == L"300.00", "recovery price is not throttled");

        plugin.QueueStreamQuotes(generation, {{0, false, {}, L"Temporary error"}, price(0, "400")});
        plugin.QueueStreamQuotes(generation, {price(0, "410"), price(1, "50")});
        plugin.DataRequired();
        check(value(0) == L"410.00" && value(1) == L"50.00", "bursts coalesce to latest quote per instrument");
        check(std::wstring(plugin.GetTooltipInfo()).find(L"Temporary error") == std::wstring::npos,
              "superseded errors do not leak into display");

        plugin.QueueStreamQuotes(generation - 1, {{0, false, {}, L"Stale generation"}});
        plugin.QueueStreamQuotes(generation, {price(kMaxInstruments, "999")});
        plugin.QueueStreamQuotes(generation, {});
        check(!plugin.hasPending_, "stale, invalid and empty updates do not publish pending data");
        plugin.DataRequired();
        check(value(0) == L"410.00", "stale generation cannot change displayed state");

        plugin.pending_.generation = generation - 1;
        plugin.pending_.quotes[0].success = false;
        plugin.hasPending_ = true;
        plugin.DataRequired();
        check(value(0) == L"410.00", "stale pending result is discarded");
        return failures ? 1 : 0;
    }
};
} // namespace cryptoprice

int main() { return cryptoprice::CryptoPricePluginTestAccess::Run(); }
