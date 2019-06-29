#pragma once


#include <iosfwd>
#include <td/utils/int_types.h>
#include <crypto/vm/cells/CellSlice.h>
#include <crypto/vm/db/StaticBagOfCellsDb.h>
#include <ton/ton-types.h>
#include <crypto/block/block.h>
#include <td/utils/format.h>
#include <crypto/block/block-auto.h>

namespace helpers{

}

using td::Ref;

template <std::size_t size>
std::ostream& operator<<(std::ostream& stream, const td::UInt<size>& x);

bool unpack_addr(std::ostream& os, Ref<vm::CellSlice> csr);

bool unpack_message(std::ostream& os, Ref<vm::Cell> msg, int mode);


std::string message_info_str(Ref<vm::Cell> msg, int mode);

td::Result<td::UInt256> get_uint256(std::string str);

td::Result<std::pair<Ref<vm::Cell>, std::shared_ptr<vm::StaticBagOfCellsDb>>> lazy_boc_deserialize(
        td::BufferSlice data);