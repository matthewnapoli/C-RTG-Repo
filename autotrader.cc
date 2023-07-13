// Copyright 2021 Optiver Asia Pacific Pty. Ltd.
//
// This file is part of Ready Trader Go.
//
//     Ready Trader Go is free software: you can redistribute it and/or
//     modify it under the terms of the GNU Affero General Public License
//     as published by the Free Software Foundation, either version 3 of
//     the License, or (at your option) any later version.
//
//     Ready Trader Go is distributed in the hope that it will be useful,
//     but WITHOUT ANY WARRANTY; without even the implied warranty of
//     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//     GNU Affero General Public License for more details.
//
//     You should have received a copy of the GNU Affero General Public
//     License along with Ready Trader Go.  If not, see
//     <https://www.gnu.org/licenses/>.

#include <map>
#include <queue>
#include <memory>
#include <string>
#include <unordered_set>
#include <array>

#include <boost/asio/io_context.hpp>
#include <ready_trader_go/logging.h>

#include "autotrader.h"

using namespace ReadyTraderGo;

RTG_INLINE_GLOBAL_LOGGER_WITH_CHANNEL(LG_AT, "AUTO")

constexpr int LOT_SIZE = 10;
constexpr int POSITION_LIMIT = 100;
constexpr int TICK_SIZE_IN_CENTS = 100;
constexpr int MIN_BID_NEARST_TICK = (MINIMUM_BID + TICK_SIZE_IN_CENTS) / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;
constexpr int MAX_ASK_NEAREST_TICK = MAXIMUM_ASK / TICK_SIZE_IN_CENTS * TICK_SIZE_IN_CENTS;

AutoTrader::AutoTrader(boost::asio::io_context& context) : BaseAutoTrader(context)
{
}

//Custom log function
void AutoTrader::positionLog()
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "=---------------------------------=";
    RLOG(LG_AT, LogLevel::LL_INFO) << "ETF Pos: " << ETF_Pos << std::endl;
    RLOG(LG_AT, LogLevel::LL_INFO) << "Future Pos: " << FTR_Pos << std::endl;
    RLOG(LG_AT, LogLevel::LL_INFO) << "ETF Bids: " << std::endl;
    for(int i = 0; i < ETF_bid_arr.size(); i++) { RLOG(LG_AT, LogLevel::LL_INFO) << "| " << ETF_bid_arr[i]; }
    RLOG(LG_AT, LogLevel::LL_INFO) << "ETF Asks: " << std::endl;
    for(int i = 0; i < ETF_ask_arr.size(); i++) { RLOG(LG_AT, LogLevel::LL_INFO) << "| " << ETF_ask_arr[i]; }
    RLOG(LG_AT, LogLevel::LL_INFO) << "Future Bids: " << std::endl;
    for(int i = 0; i < FTR_bid_arr.size(); i++) { RLOG(LG_AT, LogLevel::LL_INFO) << "| " << FTR_bid_arr[i]; }
    RLOG(LG_AT, LogLevel::LL_INFO) << "Future Asks: " << std::endl;
    for(int i = 0; i < FTR_ask_arr.size(); i++) { RLOG(LG_AT, LogLevel::LL_INFO) << "| " << FTR_ask_arr[i]; }
}

/* Function to check if midpoint prices are "far enough" subjectively to trade */
void AutoTrader::deterMineOrderStatus(std::queue<unsigned long> DIFF_recent_mp_prices)
{
    unsigned int last = DIFF_recent_mp_prices.size()-1;
    //find mean of difference of midpoint prices (ETF-FTR) of last n-1 entries
    unsigned long avgDif = 0;
    for(int i = 1; i < DIFF_recent_mp_prices.size();i++)
    {
        avgDif+=(DIFF_recent_mp_prices[i]);
    }
    avgDif/=(DIFF_recent_mp_price.size() - 1);

    //find std deviation of difference of midpoint prices of last n-1 entries
    unsigned long stdDevDif = 0;
    for(int i = 1; i < DIFF_recent_mp_prices.size(); i++)
    {
        stdDevDif += std::pow((avgDif - DIFF_recent_mp_prices[i]),2);
    }
    stdDevDif = std::pow(stdDevDif/=(DIFF_recent_mp_price.size() - 1), 1/2);

    //check if the differences are extreme enough
    if(DIFF_recent_mp_prices[last] > avgDif + 1*(stdDevDif))
    {
        ETF_Much_Greater = true;
        FTR_Much_Greater = false;
    } else if(DIFF_recent_mp_prices[last] < avgDif - 1*(stdDevDif))
    {
        ETF_Much_Greater = false;
        FTR_Much_Greater = true;
    } else{
        ETF_Much_Greater = false;
        FTR_Much_Greater = false;
    }
}


//Misc
void AutoTrader::DisconnectHandler()
{
    BaseAutoTrader::DisconnectHandler();
    RLOG(LG_AT, LogLevel::LL_INFO) << "execution connection lost";
}

//Error logger
void AutoTrader::ErrorMessageHandler(unsigned long clientOrderId,
                                     const std::string& errorMessage)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "error with order " << clientOrderId << ": " << errorMessage;
    if (clientOrderId != 0 && ((mAsks.count(clientOrderId) == 1) || (mBids.count(clientOrderId) == 1)))
    {
        OrderStatusMessageHandler(clientOrderId, 0, 0, 0);
    }
}

//Hedge Function Logger
void AutoTrader::HedgeFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "hedge order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " average price in cents";
}

//Called 4 times a second by exchange (2 time Instrument = ETF, 2 times Instrument = FUTURE)
void AutoTrader::OrderBookMessageHandler(Instrument instrument,
                                         unsigned long sequenceNumber,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                         const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order book received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];  

    if (instrument == Instrument::ETF)
    {
        //retrieving data
        ETF_ask_arr = askPrices;
        ETF_bid_arr = bidPrices;
        ETF_bestAsk = askPrices[0];
        ETF_bestBid = bidPrices[0];
        //storing midprice
        ETF_midprice = (ETF_bestAsk + ETF_bestBid) / 2;

        if(ETF_recent_mp_prices.size() <= 31) //rolling avg for thirty
        {
            ETF_recent_mp_prices.push_back(ETF_midprice);   
        }
        else
        {
            ETF_recent_mp_prices.pop();
            ETF_recent_mp_prices.push_back(ETF_midprice);
        }

        if(ETF_midprice != 0 && FTR_midprice != 0) //if game has started
        {
            //log
            positionLog();
            //if theres an even amount of samples, take the difference and add it to sample of differences then check if we should trade
            if(ETF_recent_mp_prices.size() == FTR_recent_mp_prices.size())
            {
                DIFF_recent_mp_prices.push_back(ETF_recent_mp_prices[ETF_recent_mp_prices.size()-1] - FTR_recent_mp_prices[FTR_recent_mp_prices.size()-1]);
                deterMineOrderStatus(DIFF_recent_mp_prices); //<== determines ETF_Much_Greater/FTR_Much_Greater
            }
            

            if(ETF_Much_Greater == true && ETF_Pos - std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) > -100)
            {
                //send a Ask/Sell order at the best bid
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::SELL, ETF_bestBid, std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_ask_arr.push_back(ETF_bestBid);
                my_ETF_ask_ids.push_back(ETF_tempID)
                my_ETF_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Buy order at the best ask for FTR
                unsigned int tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::BUY, FTR_bestAsk, std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_bid_arr.push_back(FTR_bestAsk);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
                
            } else if(FTR_Much_Greater == true && ETF_Pos + std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) < 100)
            {
                 //send an ETF Bid/Buy order at the best ask
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::BUY, ETF_bestAsk, std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_bid_arr.push_back(ETF_bestAsk);
                my_ETF_bid_ids.push_back(tempID);
                my_etf_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Ask/Sell order at the best bid for FTR
                unsigned long tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::SELL, FTR_bestBid, std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_ask_arr.push_back(FTR_bestBid);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
            }
        }
    }
//=------------------------------------------------------------------------------------------------------------------------------------=
    if (instrument == Instrument::FUTURE)
    {
        FTR_ask_arr = askPrices;
        FTR_bid_arr = bidPrices;
        FTR_bestAsk = askPrices[0];
        FTR_bestBid = bidPrices[0];
        //storing midprice
        FTR_midprice = (FTR_bestAsk + FTR_bestBid) / 2;

        if(FTR_recent_mp_prices.size() <= 31) //rolling avg for thirty
        {
            FTR_recent_mp_prices.push_back(FTR_midprice);   
        }
        else
        {
            FTR_recent_mp_prices.pop();
            FTR_recent_mp_prices.push_back(FTR_midprice);
        }

        if(ETF_midprice != 0 && FTR_midprice != 0) //if game has started
        {
            //log
            positionLog();
            //if theres an even amount of samples, take the difference and add it to sample of differences then check if we should trade
            if(ETF_recent_mp_prices.size() == FTR_recent_mp_prices.size())
            {
                DIFF_recent_mp_prices.push_back(ETF_recent_mp_prices[ETF_recent_mp_prices.size()-1] - FTR_recent_mp_prices[FTR_recent_mp_prices.size()-1]);
                deterMineOrderStatus(DIFF_recent_mp_prices); //<== determines ETF_Much_Greater/FTR_Much_Greater
            }
            

            if(ETF_Much_Greater == true && ETF_Pos - std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) > -100)
            {
                //send a Ask/Sell order at the best bid
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::SELL, ETF_bestBid, std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_ask_arr.push_back(ETF_bestBid);
                my_ETF_ask_ids.push_back(ETF_tempID)
                my_ETF_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Buy order at the best ask for FTR
                unsigned int tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::BUY, FTR_bestAsk, std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_bid_arr.push_back(FTR_bestAsk);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
                
            } else if(FTR_Much_Greater == true && ETF_Pos + std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) < 100)
            {
                 //send an ETF Bid/Buy order at the best ask
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::BUY, ETF_bestAsk, std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_bid_arr.push_back(ETF_bestAsk);
                my_ETF_bid_ids.push_back(tempID);
                my_etf_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Ask/Sell order at the best bid for FTR
                unsigned long tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::SELL, FTR_bestBid, std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_ask_arr.push_back(FTR_bestBid);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
            }
        }
    }
}



//Order Message Logger
void AutoTrader::OrderFilledMessageHandler(unsigned long clientOrderId,
                                           unsigned long price,
                                           unsigned long volume)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "order " << clientOrderId << " filled for " << volume
                                   << " lots at $" << price << " cents";
    if (mAsks.count(clientOrderId) == 1)
    {
        mPosition -= (long)volume;
        SendHedgeOrder(mNextMessageId++, Side::BUY, MAX_ASK_NEAREST_TICK, volume);
    }
    else if (mBids.count(clientOrderId) == 1)
    {
        mPosition += (long)volume;
        SendHedgeOrder(mNextMessageId++, Side::SELL, MIN_BID_NEARST_TICK, volume);
    }
}


//Called when error rn (can use)
void AutoTrader::OrderStatusMessageHandler(unsigned long clientOrderId,
                                           unsigned long fillVolume,
                                           unsigned long remainingVolume,
                                           signed long fees)
{
    if (remainingVolume == 0)
    {
        if (clientOrderId == mAskId)
        {
            mAskId = 0;
        }
        else if (clientOrderId == mBidId)
        {
            mBidId = 0;
        }

        mAsks.erase(clientOrderId);
        mBids.erase(clientOrderId);
    }
}


//Updated from exchange
void AutoTrader::TradeTicksMessageHandler(Instrument instrument,
                                          unsigned long sequenceNumber,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& askVolumes,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidPrices,
                                          const std::array<unsigned long, TOP_LEVEL_COUNT>& bidVolumes)
{
    RLOG(LG_AT, LogLevel::LL_INFO) << "trade ticks received for " << instrument << " instrument"
                                   << ": ask prices: " << askPrices[0]
                                   << "; ask volumes: " << askVolumes[0]
                                   << "; bid prices: " << bidPrices[0]
                                   << "; bid volumes: " << bidVolumes[0];

    if (instrument == Instrument::ETF)
    {
        //retrieving data
        ETF_ask_arr = askPrices;
        ETF_bid_arr = bidPrices;
        ETF_bestAsk = askPrices[0];
        ETF_bestBid = bidPrices[0];
        //storing midprice
        ETF_midprice = (ETF_bestAsk + ETF_bestBid) / 2;

        if(ETF_recent_mp_prices.size() <= 31) //rolling avg for thirty
        {
            ETF_recent_mp_prices.push_back(ETF_midprice);   
        }
        else
        {
            ETF_recent_mp_prices.pop();
            ETF_recent_mp_prices.push_back(ETF_midprice);
        }

        if(ETF_midprice != 0 && FTR_midprice != 0) //if game has started
        {
            //log
            positionLog();
            //if theres an even amount of samples, take the difference and add it to sample of differences then check if we should trade
            if(ETF_recent_mp_prices.size() == FTR_recent_mp_prices.size())
            {
                DIFF_recent_mp_prices.push_back(ETF_recent_mp_prices[ETF_recent_mp_prices.size()-1] - FTR_recent_mp_prices[FTR_recent_mp_prices.size()-1]);
                deterMineOrderStatus(DIFF_recent_mp_prices); //<== determines ETF_Much_Greater/FTR_Much_Greater
            }
            

            if(ETF_Much_Greater == true && ETF_Pos - std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) > -100)
            {
                //send a Ask/Sell order at the best bid
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::SELL, ETF_bestBid, std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_ask_arr.push_back(ETF_bestBid);
                my_ETF_ask_ids.push_back(ETF_tempID)
                my_ETF_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Buy order at the best ask for FTR
                unsigned int tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::BUY, FTR_bestAsk, std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_bid_arr.push_back(FTR_bestAsk);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
                
            } else if(FTR_Much_Greater == true && ETF_Pos + std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) < 100)
            {
                 //send an ETF Bid/Buy order at the best ask
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::BUY, ETF_bestAsk, std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_bid_arr.push_back(ETF_bestAsk);
                my_ETF_bid_ids.push_back(tempID);
                my_etf_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Ask/Sell order at the best bid for FTR
                unsigned long tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::SELL, FTR_bestBid, std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_ask_arr.push_back(FTR_bestBid);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
            }
        }
    }
//=------------------------------------------------------------------------------------------------------------------------------------=
    if (instrument == Instrument::FUTURE)
    {
        FTR_ask_arr = askPrices;
        FTR_bid_arr = bidPrices;
        FTR_bestAsk = askPrices[0];
        FTR_bestBid = bidPrices[0];
        //storing midprice
        FTR_midprice = (FTR_bestAsk + FTR_bestBid) / 2;

        if(FTR_recent_mp_prices.size() <= 31) //rolling avg for thirty
        {
            FTR_recent_mp_prices.push_back(FTR_midprice);   
        }
        else
        {
            FTR_recent_mp_prices.pop();
            FTR_recent_mp_prices.push_back(FTR_midprice);
        }

        if(ETF_midprice != 0 && FTR_midprice != 0) //if game has started
        {
            //log
            positionLog();
            //if theres an even amount of samples, take the difference and add it to sample of differences then check if we should trade
            if(ETF_recent_mp_prices.size() == FTR_recent_mp_prices.size())
            {
                DIFF_recent_mp_prices.push_back(ETF_recent_mp_prices[ETF_recent_mp_prices.size()-1] - FTR_recent_mp_prices[FTR_recent_mp_prices.size()-1]);
                deterMineOrderStatus(DIFF_recent_mp_prices); //<== determines ETF_Much_Greater/FTR_Much_Greater
            }
            

            if(ETF_Much_Greater == true && ETF_Pos - std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) > -100)
            {
                //send a Ask/Sell order at the best bid
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::SELL, ETF_bestBid, std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_ask_arr.push_back(ETF_bestBid);
                my_ETF_ask_ids.push_back(ETF_tempID)
                my_ETF_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Buy order at the best ask for FTR
                unsigned int tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::BUY, FTR_bestAsk, std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_bid_arr.push_back(FTR_bestAsk);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_ask_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
                
            } else if(FTR_Much_Greater == true && ETF_Pos + std::min((unsigned long)LOT_SIZE, ETF_bid_arr[0]) < 100)
            {
                 //send an ETF Bid/Buy order at the best ask
                unsigned long tempID = mAskID;
                SendInsertOrder(mAskID, Side::BUY, ETF_bestAsk, std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_ETF_bid_arr.push_back(ETF_bestAsk);
                my_ETF_bid_ids.push_back(tempID);
                my_etf_bid_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, ETF_ask_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Etf Ask/Sell Placed~~~~~~~~";  
                positionLog();
                //send a Ask/Sell order at the best bid for FTR
                unsigned long tempID2 = mAskID;
                SendHedgeOrder(mAskID, SIDE::SELL, FTR_bestBid, std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]), Lifespan::GOOD_FOR_DAY);
                //adding it to our order book at the price and quanity we ordered for 
                my_FTR_ask_arr.push_back(FTR_bestBid);
                my_FTR_ask_ids.push_back(tempID2);
                my_FTR_ask_vol_arr.push_back(std::min((unsigned long)LOT_SIZE, FTR_bid_vol_arr[0]));
                //log
                RLOG(LG_AT, LogLevel::LL_INFO) << "\n~~~~~~~~After Hedge Bid/Buy Placed~~~~~~~~";  
                positionLog();
            }
        }
    }
}
