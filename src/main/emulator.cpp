/*
 * emulator.cpp
 *
 *  Created on: 2. 6. 2019
 *      Author: ondra
 */


#include "emulator.h"

#include <chrono>

#include "../shared/logOutput.h"
EmulatorAPI::EmulatorAPI(IStockApi &datasrc, double initial_currency):datasrc(datasrc)
	,prevId(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
				).count())
	,initial_currency(initial_currency)
	,log("emulator")
{

}

double EmulatorAPI::readBalance(const std::string_view &symb, double defval) {
	try {
		return datasrc.getBalance(symb);
	} catch (std::exception &e) {
		log.warning("Balance for $1 is not available, setting to $2 - ", symb, defval, e.what());
		return defval;
	}

}

double EmulatorAPI::getBalance(const std::string_view & symb) {

	if (balance_symb == symb) {
		if (initial_read_balance) {
			initial_read_balance = false;
			balance = readBalance(symb, 0);
		}
		return balance;
	} else if (currency_symb == symb) {
		if (initial_read_currency) {
			initial_read_currency = false;
			currency = readBalance(symb, initial_currency);
		}
		return currency;
	} else {
		return 0;
	}
}

EmulatorAPI::TradeHistory EmulatorAPI::getTrades(json::Value lastId, std::uintptr_t ,
		const std::string_view &)  {

	return TradeHistory(std::move(trades));
}

EmulatorAPI::Orders EmulatorAPI::getOpenOrders(const std::string_view & pair) {
	simulation(datasrc.getTicker(pair));
	return Orders(orders.begin(), orders.end());
}

EmulatorAPI::Ticker EmulatorAPI::getTicker(const std::string_view & pair) {
	this->pair = pair;
	Ticker tk = datasrc.getTicker(pair);
	simulation(tk);
	return tk;
}

json::Value EmulatorAPI::placeOrder(const std::string_view & pair,
		double size, double price,json::Value clientId,
		json::Value replaceId,double replaceSize) {

	Order order{genID(), clientId, size, price};

	if (replaceId.defined()) {
		auto iter = std::find_if(orders.begin(), orders.end(), [&](const Order &o){
			return o.id == replaceId;
		});
		if (iter != orders.end()) {
			*iter = order;
			return iter->id;
		} else {
			return nullptr;
		}
	} else {
		orders.push_back(order);
		return order.id;
	}
}

EmulatorAPI::MarketInfo EmulatorAPI::getMarketInfo(const std::string_view & pair) {
	minfo = datasrc.getMarketInfo(pair);
	balance_symb = minfo.asset_symbol;
	currency_symb = minfo.currency_symbol;
	margin = minfo.leverage > 0;
	return minfo;
}


double EmulatorAPI::getFees(const std::string_view &pair) {
	minfo.fees = datasrc.getFees(pair);
	return minfo.fees;
}

std::vector<std::string> EmulatorAPI::getAllPairs() {
	return datasrc.getAllPairs();
}

void EmulatorAPI::simulation(const Ticker &tk) {

	double cur = tk.last;
	Orders left_orders;
	for (auto &&o: orders) {

		double diffp = cur - o.price;
		double sm = diffp * o.size;
		if (sm > 0) {
			left_orders.push_back(std::move(o));
		} else {
			auto tm = tk.time;
			IStockApi::Trade tr {
				genID(),
				static_cast<std::size_t>(tm),
				o.size,
				o.price,
				o.size,
				o.price
			};
			minfo.removeFees(tr.eff_size, tr.eff_price);
			trades.push_back(tr);
			ondra_shared::logInfo("Emulator Trade: $1 on $2", o.size, o.price);
			if (minfo.leverage > 0) {
				if (balance) {
					double open_price = margin_currency / balance;
					double price_diff = o.price - open_price;
					currency += balance * price_diff;
				}
				margin_currency += margin_currency - tr.size * tr.price;
			} else {
				currency -= tr.size * tr.eff_price;
			}
			balance +=tr.eff_size;
		}

	}

	std::swap(orders, left_orders);
}

bool EmulatorAPI::reset() {
	if (!datasrc.reset()) return false;
	if (!pair.empty()) getTicker(pair);
	return true;
}

bool EmulatorAPI::isTest() const {
	return true;
}

std::size_t EmulatorAPI::genID() {
	return ++prevId;
}
