/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <fc/smart_ref_impl.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/buyback.hpp>
#include <graphene/chain/buyback_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/internal_exceptions.hpp>
#include <graphene/chain/special_authority.hpp>
#include <graphene/chain/special_authority_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <algorithm>

namespace graphene { namespace chain {

void verify_authority_accounts( const database& db, const authority& a )
{
   const auto& chain_params = db.get_global_properties().parameters;
   GRAPHENE_ASSERT(
      a.num_auths() <= chain_params.maximum_authority_membership,
      internal_verify_auth_max_auth_exceeded,
      "Maximum authority membership exceeded" );
   for( const auto& acnt : a.account_auths )
   {
      GRAPHENE_ASSERT( db.find_object( acnt.first ) != nullptr,
         internal_verify_auth_account_not_found,
         "Account ${a} specified in authority does not exist",
         ("a", acnt.first) );
   }
}

void verify_account_votes( const database& db, const account_options& options )
{
   // ensure account's votes satisfy requirements
   // NB only the part of vote checking that requires chain state is here,
   // the rest occurs in account_options::validate()

   const auto& gpo = db.get_global_properties();
   const auto& chain_params = gpo.parameters;

   FC_ASSERT( options.num_witness <= chain_params.maximum_witness_count,
              "Voted for more witnesses than currently allowed (${c})", ("c", chain_params.maximum_witness_count) );
   FC_ASSERT( options.num_committee <= chain_params.maximum_committee_count,
              "Voted for more committee members than currently allowed (${c})", ("c", chain_params.maximum_committee_count) );

   // uint32_t max_vote_id = gpo.next_available_vote_id;

   {
      const auto& committee_idx = db.get_index_type<committee_member_index>().indices().get<by_vote_id>();
      const auto& witness_idx = db.get_index_type<witness_index>().indices().get<by_vote_id>();
      for ( auto id : options.votes ) {
         switch ( id.type() ) {
            case vote_id_type::committee:
               FC_ASSERT( committee_idx.find(id) != committee_idx.end() );
               break;
            case vote_id_type::witness:
               FC_ASSERT( witness_idx.find(id) != witness_idx.end());
               break;
            default:
               FC_THROW( "Invalid Vote Type: ${id}", ("id", id) );
               break;
         }
      }
   }
}


void_result account_create_evaluator::do_evaluate( const account_create_operation& op )
{ try {
   database& d = db();

   FC_ASSERT( d.find_object(op.options.voting_account), "Invalid proxy account specified." );
   FC_ASSERT( fee_paying_account->is_lifetime_member(), "Only Lifetime members may register an account." );

   try
   {
      verify_authority_accounts( d, op.owner );
      verify_authority_accounts( d, op.active );
   }
   GRAPHENE_RECODE_EXC( internal_verify_auth_max_auth_exceeded, account_create_max_auth_exceeded )
   GRAPHENE_RECODE_EXC( internal_verify_auth_account_not_found, account_create_auth_account_not_found )

   if( op.extensions.value.owner_special_authority.valid() )
      evaluate_special_authority( d, *op.extensions.value.owner_special_authority );
   if( op.extensions.value.active_special_authority.valid() )
      evaluate_special_authority( d, *op.extensions.value.active_special_authority );

   verify_account_votes( d, op.options );

   auto& acnt_indx = d.get_index_type<account_index>();
   if( op.name.size() )
   {
      auto current_account_itr = acnt_indx.indices().get<by_name>().find( op.name );
      FC_ASSERT( current_account_itr == acnt_indx.indices().get<by_name>().end() );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type account_create_evaluator::do_apply( const account_create_operation& o )
{ try {

   //database& d = db();

   const auto& new_acnt_object = db().create<account_object>( [&]( account_object& obj ){
         obj.registrar = o.registrar;

         //auto& params = db().get_global_properties().parameters;

         obj.name             = o.name;
         obj.owner            = o.owner;
         obj.active           = o.active;
         obj.options          = o.options;
         obj.statistics = db().create<account_statistics_object>([&](account_statistics_object& s){s.owner = obj.id;}).id;

         if( o.extensions.value.owner_special_authority.valid() )
            obj.owner_special_authority = *(o.extensions.value.owner_special_authority);
         if( o.extensions.value.active_special_authority.valid() )
            obj.active_special_authority = *(o.extensions.value.active_special_authority);
   });

   const auto& dynamic_properties = db().get_dynamic_global_properties();
   db().modify(dynamic_properties, [](dynamic_global_property_object& p) {
      ++p.accounts_registered_this_interval;
   });

   const auto& global_properties = db().get_global_properties();
   if( dynamic_properties.accounts_registered_this_interval %
       global_properties.parameters.accounts_per_fee_scale == 0 )
      db().modify(global_properties, [&dynamic_properties](global_property_object& p) {
         p.parameters.current_fees->get<account_create_operation>().basic_fee <<= p.parameters.account_fee_scale_bitshifts;
      });

   if(    o.extensions.value.owner_special_authority.valid()
       || o.extensions.value.active_special_authority.valid() )
   {
      db().create< special_authority_object >( [&]( special_authority_object& sa )
      {
         sa.account = new_acnt_object.id;
      } );
   }

   return new_acnt_object.id;
} FC_CAPTURE_AND_RETHROW((o)) }


void_result account_update_evaluator::do_evaluate( const account_update_operation& o )
{ try {
   database& d = db();

   try
   {
      if( o.owner )  verify_authority_accounts( d, *o.owner );
      if( o.active ) verify_authority_accounts( d, *o.active );
   }
   GRAPHENE_RECODE_EXC( internal_verify_auth_max_auth_exceeded, account_update_max_auth_exceeded )
   GRAPHENE_RECODE_EXC( internal_verify_auth_account_not_found, account_update_auth_account_not_found )

   if( o.extensions.value.owner_special_authority.valid() )
      evaluate_special_authority( d, *o.extensions.value.owner_special_authority );
   if( o.extensions.value.active_special_authority.valid() )
      evaluate_special_authority( d, *o.extensions.value.active_special_authority );

   acnt = &o.account(d);

   if( o.new_options.valid() )
      verify_account_votes( d, *o.new_options );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result account_update_evaluator::do_apply( const account_update_operation& o )
{ try {
   database& d = db();
   bool sa_before, sa_after;
   d.modify( *acnt, [&](account_object& a){
      if( o.owner )
      {
         a.owner = *o.owner;
         a.top_n_control_flags = 0;
      }
      if( o.active )
      {
         a.active = *o.active;
         a.top_n_control_flags = 0;
      }
      if( o.new_options ) a.options = *o.new_options;
      sa_before = a.has_special_authority();
      if( o.extensions.value.owner_special_authority.valid() )
      {
         a.owner_special_authority = *(o.extensions.value.owner_special_authority);
         a.top_n_control_flags = 0;
      }
      if( o.extensions.value.active_special_authority.valid() )
      {
         a.active_special_authority = *(o.extensions.value.active_special_authority);
         a.top_n_control_flags = 0;
      }
      sa_after = a.has_special_authority();
   });

   if( sa_before && (!sa_after) )
   {
      const auto& sa_idx = d.get_index_type< special_authority_index >().indices().get<by_account>();
      auto sa_it = sa_idx.find( o.account );
      assert( sa_it != sa_idx.end() );
      d.remove( *sa_it );
   }
   else if( (!sa_before) && sa_after )
   {
      d.create< special_authority_object >( [&]( special_authority_object& sa )
      {
         sa.account = o.account;
      } );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result account_upgrade_evaluator::do_evaluate(const account_upgrade_evaluator::operation_type& o)
{ try {
   database& d = db();

   account = &d.get(o.account_to_upgrade);
   FC_ASSERT(!account->is_lifetime_member());

   return {};
//} FC_CAPTURE_AND_RETHROW( (o) ) }
} FC_RETHROW_EXCEPTIONS( error, "Unable to upgrade account '${a}'", ("a",o.account_to_upgrade(db()).name) ) }

void_result account_upgrade_evaluator::do_apply(const account_upgrade_evaluator::operation_type& o)
{ try {
   database& d = db();

   d.modify(*account, [&](account_object& a) {
      if( o.upgrade_to_lifetime_member )
      {
         // Upgrade to lifetime member. I don't care what the account was before.
         a.statistics(d).process_fees(a, d);
         a.membership_expiration_date = time_point_sec::maximum();
         a.registrar = a.get_id();
      } else if( a.is_annual_member(d.head_block_time()) ) {
         // Renew an annual subscription that's still in effect.
         FC_ASSERT( false, "Deprecated" );
         FC_ASSERT(a.membership_expiration_date - d.head_block_time() < fc::days(3650),
                   "May not extend annual membership more than a decade into the future.");
         a.membership_expiration_date += fc::days(365);
      } else {
         // Upgrade from basic account.
         FC_ASSERT( false, "Deprecated" );
         a.statistics(d).process_fees(a, d);
         assert(a.is_basic_account(d.head_block_time()));
         a.membership_expiration_date = d.head_block_time() + fc::days(365);
      }
   });

   return {};
} FC_RETHROW_EXCEPTIONS( error, "Unable to upgrade account '${a}'", ("a",o.account_to_upgrade(db()).name) ) }

} } // graphene::chain
