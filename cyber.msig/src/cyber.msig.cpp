#include <cyber.msig/cyber.msig.hpp>
#include <eosio/action.hpp>
#include <eosio/permission.hpp>
#include <eosio/crypto.hpp>

namespace eosio {

void multisig::propose( ignore<name> proposer,
                        ignore<name> proposal_name,
                        ignore<std::vector<permission_level>> requested,
                        ignore<transaction> trx,
                        ignore<const eosio::binary_extension<std::string>> description )
{
   name _proposer;
   name _proposal_name;
   std::vector<permission_level> _requested;
   transaction_header _trx_header;

   _ds >> _proposer >> _proposal_name >> _requested;

   const char* trx_pos = _ds.pos();
   size_t size    = _ds.remaining();
   _ds >> _trx_header;

   require_auth( _proposer );
   eosio::check( _trx_header.expiration >= eosio::current_time_point(), "transaction expired" );
   //eosio::check( trx_header.actions.size() > 0, "transaction must have at least one action" );

   proposals proptable( _self, _proposer.value );
   eosio::check( proptable.find( _proposal_name.value ) == proptable.end(), "proposal with the same name exists" );

   auto packed_requested = pack(_requested);
   auto res = eosio::check_transaction_authorization( trx_pos, size,
                                                 (const char*)0, 0,
                                                 packed_requested.data(), packed_requested.size()
                                               );
   eosio::check( res > 0, "transaction authorization failed" );

   std::vector<char> pkd_trans;
   pkd_trans.resize(size);
   memcpy((char*)pkd_trans.data(), trx_pos, size);
   proptable.emplace( _proposer, [&]( auto& prop ) {
      prop.proposal_name       = _proposal_name;
      prop.packed_transaction  = pkd_trans;
   });

   approvals apptable(  _self, _proposer.value );
   apptable.emplace( _proposer, [&]( auto& a ) {
      a.proposal_name       = _proposal_name;
      a.requested_approvals.reserve( _requested.size() );
      for ( auto& level : _requested ) {
         a.requested_approvals.push_back( approval{ level, eosio::time_point() } );
      }
   });
}

void multisig::approve( name proposer, name proposal_name, permission_level level,
                        const eosio::binary_extension<eosio::checksum256>& proposal_hash )
{
   require_auth( level );

   if( proposal_hash ) {
      proposals proptable( _self, proposer.value );
      auto& prop = proptable.get( proposal_name.value, "proposal not found" );
      assert_sha256( prop.packed_transaction.data(), prop.packed_transaction.size(), *proposal_hash );
   }

   approvals apptable(  _self, proposer.value );
   auto apps_it = apptable.find( proposal_name.value );
   if ( apps_it != apptable.end() ) {
      auto itr = std::find_if( apps_it->requested_approvals.begin(), apps_it->requested_approvals.end(), [&](const approval& a) { return a.level == level; } );
      eosio::check( itr != apps_it->requested_approvals.end(), "approval is not on the list of requested approvals" );

      apptable.modify( apps_it, proposer, [&]( auto& a ) {
            a.provided_approvals.push_back( approval{ level, current_time_point() } );
            a.requested_approvals.erase( itr );
         });
   } else {
      eosio::check(false, "proposal not found");
   }
}

void multisig::unapprove( name proposer, name proposal_name, permission_level level ) {
   require_auth( level );

   approvals apptable(  _self, proposer.value );
   auto apps_it = apptable.find( proposal_name.value );
   if ( apps_it != apptable.end() ) {
      auto itr = std::find_if( apps_it->provided_approvals.begin(), apps_it->provided_approvals.end(), [&](const approval& a) { return a.level == level; } );
      eosio::check( itr != apps_it->provided_approvals.end(), "no approval previously granted" );
      apptable.modify( apps_it, proposer, [&]( auto& a ) {
            a.requested_approvals.push_back( approval{ level, current_time_point() } );
            a.provided_approvals.erase( itr );
         });
   } else {
      eosio::check(false, "proposal not found");
   }
}

void multisig::cancel( name proposer, name proposal_name, name canceler ) {
   require_auth( canceler );

   proposals proptable( _self, proposer.value );
   auto& prop = proptable.get( proposal_name.value, "proposal not found" );

   if( canceler != proposer ) {
      eosio::check( unpack<transaction_header>( prop.packed_transaction ).expiration < current_time_point(), "cannot cancel until expiration" );
   }
   proptable.erase(prop);

   //remove from new table
   approvals apptable(  _self, proposer.value );
   auto apps_it = apptable.find( proposal_name.value );
   eosio::check(apps_it != apptable.end(), "proposal not found");
   apptable.erase(apps_it);
}

void multisig::exec( name proposer, name proposal_name, name executer ) {
   require_auth( executer );

   proposals proptable( _self, proposer.value );
   auto& prop = proptable.get( proposal_name.value, "proposal not found" );
   transaction_header trx_header;
   datastream<const char*> ds( prop.packed_transaction.data(), prop.packed_transaction.size() );
   ds >> trx_header;
   eosio::check( trx_header.expiration >= current_time_point(), "transaction expired" );

   approvals apptable(  _self, proposer.value );
   auto apps_it = apptable.find( proposal_name.value );
   std::vector<permission_level> approvals;
   invalidations inv_table( _self, _self.value );
   if ( apps_it != apptable.end() ) {
      approvals.reserve( apps_it->provided_approvals.size() );
      for ( auto& p : apps_it->provided_approvals ) {
         auto it = inv_table.find( p.level.actor.value );
         if ( it == inv_table.end() || it->last_invalidation_time < p.time ) {
            approvals.push_back(p.level);
         }
      }
      apptable.erase(apps_it);
   } else {
      eosio::check(false, "proposal not found");
   }
   auto packed_provided_approvals = pack(approvals);
   auto res = eosio::check_transaction_authorization( prop.packed_transaction.data(), prop.packed_transaction.size(),
                                                 (const char*)0, 0,
                                                 packed_provided_approvals.data(), packed_provided_approvals.size()
                                                 );
   eosio::check( res > 0, "transaction authorization failed" );

   eosio::send_deferred( (uint128_t(proposer.value) << 64) | proposal_name.value, executer,
                  prop.packed_transaction.data(), prop.packed_transaction.size() );

   proptable.erase(prop);
}

void multisig::invalidate( name account ) {
   require_auth( account );
   invalidations inv_table( _self, _self.value );
   auto it = inv_table.find( account.value );
   if ( it == inv_table.end() ) {
      inv_table.emplace( account, [&](auto& i) {
            i.account = account;
            i.last_invalidation_time = eosio::current_time_point();
         });
   } else {
      inv_table.modify( it, account, [&](auto& i) {
            i.last_invalidation_time = eosio::current_time_point();
         });
   }
}

} /// namespace eosio
