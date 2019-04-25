#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/singleton.hpp>

namespace cyber {
using eosio::name;
using eosio::contract;
using eosio::public_key;

class [[contract("cyber.govern")]] govern : public contract {
    
struct structures {
    struct [[eosio::table("state")]] state_info {
        uint32_t block_num = 0;
        uint16_t last_producers_num = 0;
        uint16_t active_producers_num = 0;
        std::vector<name> pending_active_producers;
        std::vector<name> ignored_producers; //for testing
        int64_t target_emission_per_block = 0;
        int64_t funds = 0;
    };

    struct [[eosio::table]] producer {
        name account;
        int64_t votes;
        uint32_t last_block_produced = 0;
        uint32_t commencement_block = 0;
        int64_t pending_balance = 0; //can be negative
        int64_t confirmed_balance = 0;
        uint64_t primary_key()const { return account.value; }
        bool is_active() const { return static_cast<bool>(commencement_block); };
        bool is_elected() const { return votes >= 0; };
    };
};

    using state_singleton = eosio::singleton<"governstate"_n, structures::state_info>;
    using producers = eosio::multi_index<"producer"_n, structures::producer>;
    
    struct change_of_participants {
        std::vector<name> hired;
        std::vector<name> fired;
    };
    
    change_of_participants get_change_of_producers(producers& producers_table, std::vector<name> new_producers, bool active_only);
    void shrink_to_active_producers(producers& producers_table, std::vector<std::pair<name, public_key> >& arg);
    void update_and_reward_producers(producers& producers_table, structures::state_info& s);
    void reward_workers(structures::state_info& s);
    void sum_up(producers& producers_table);
    bool maybe_promote_active_producers(const structures::state_info& s);
    int64_t get_target_emission_per_block(int64_t supply) const;
    
public:
    using contract::contract;
    
    [[eosio::action]] void onblock(name producer);
    [[eosio::action]] void setactprods(std::vector<name> pending_active_producers);
    [[eosio::action]] void setignored(std::vector<name> ignored_producers);
};

} /// cyber
