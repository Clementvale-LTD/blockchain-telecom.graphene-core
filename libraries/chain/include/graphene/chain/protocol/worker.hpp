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
#pragma once
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/memo.hpp>

namespace graphene { namespace chain {

struct service_create_operation : public base_operation
{     
   struct fee_parameters_type { uint64_t fee = 5000*GRAPHENE_BLOCKCHAIN_PRECISION; };

   asset                fee;
   account_id_type      owner;
   string               name;
   memo_group           p_memo;

   account_id_type   fee_payer()const { return owner; }
   void              validate()const;
};  

struct service_update_operation : public base_operation
{     
   struct fee_parameters_type { uint64_t fee = 5000*GRAPHENE_BLOCKCHAIN_PRECISION; };

   asset                fee;
   account_id_type      owner;
   service_id_type      service_to_update;

   memo_group           p_memo;

   account_id_type   fee_payer()const { return owner; }
   void              validate()const;
};  

} }

FC_REFLECT( graphene::chain::service_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::service_create_operation,
            (fee)(owner)(name)(p_memo) )

FC_REFLECT( graphene::chain::service_update_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::service_update_operation,
            (fee)(owner)(service_to_update)(p_memo) )

