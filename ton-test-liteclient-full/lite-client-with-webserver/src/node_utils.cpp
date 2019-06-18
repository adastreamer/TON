#include "node_utils.hpp"

template <std::size_t size>
std::ostream& operator<<(std::ostream& stream, const td::UInt<size>& x) {
    for (size_t i = 0; i < size / 8; i++) {
        stream << td::format::hex_digit((x.raw[i] >> 4) & 15) << td::format::hex_digit(x.raw[i] & 15);
    }

    return stream;
}

bool unpack_addr(std::ostream& os, Ref<vm::CellSlice> csr) {
    ton::WorkchainId wc;
    ton::StdSmcAddress addr;
    if (!block::tlb::t_MsgAddressInt.extract_std_address(std::move(csr), wc, addr)) {
        os << "<cannot unpack address>";
        return false;
    }
    os << wc << ":" << addr.to_hex();
    return true;
}

bool unpack_message(std::ostream& os, Ref<vm::Cell> msg, int mode) {
    if (msg.is_null()) {
        os << "<message not found>";
        return true;
    }
    vm::CellSlice cs{vm::NoVmOrd(), msg};
    block::gen::CommonMsgInfo info;
    Ref<vm::CellSlice> src, dest;
    switch (block::gen::t_CommonMsgInfo.get_tag(cs)) {
        case block::gen::CommonMsgInfo::ext_in_msg_info: {
            block::gen::CommonMsgInfo::Record_ext_in_msg_info info;
            if (!tlb::unpack(cs, info)) {
                LOG(DEBUG) << "cannot unpack inbound external message";
                return false;
            }
            os << "EXT-IN-MSG";
            if (!(mode & 2)) {
                os << " TO: ";
                if (!unpack_addr(os, std::move(info.dest))) {
                    return false;
                }
            }
            return true;
        }
        case block::gen::CommonMsgInfo::ext_out_msg_info: {
            block::gen::CommonMsgInfo::Record_ext_out_msg_info info;
            if (!tlb::unpack(cs, info)) {
                LOG(DEBUG) << "cannot unpack outbound external message";
                return false;
            }
            os << "EXT-OUT-MSG";
            if (!(mode & 1)) {
                os << " FROM: ";
                if (!unpack_addr(os, std::move(info.src))) {
                    return false;
                }
            }
            os << " LT:" << info.created_lt << " UTIME:" << info.created_at;
            return true;
        }
        case block::gen::CommonMsgInfo::int_msg_info: {
            block::gen::CommonMsgInfo::Record_int_msg_info info;
            if (!tlb::unpack(cs, info)) {
                LOG(DEBUG) << "cannot unpack internal message";
                return false;
            }
            os << "INT-MSG";
            if (!(mode & 1)) {
                os << " FROM: ";
                if (!unpack_addr(os, std::move(info.src))) {
                    return false;
                }
            }
            if (!(mode & 2)) {
                os << " TO: ";
                if (!unpack_addr(os, std::move(info.dest))) {
                    return false;
                }
            }
            os << " LT:" << info.created_lt << " UTIME:" << info.created_at;
            td::RefInt256 value;
            Ref<vm::Cell> extra;
            if (!block::unpack_CurrencyCollection(info.value, value, extra)) {
                LOG(ERROR) << "cannot unpack message value";
                return false;
            }
            os << " VALUE:" << value;
            if (extra.not_null()) {
                os << "+extra";
            }
            return true;
        }
        default:
            LOG(ERROR) << "cannot unpack message";
            return false;
    }
}


std::string message_info_str(Ref<vm::Cell> msg, int mode) {
    std::ostringstream os;
    if (!unpack_message(os, msg, mode)) {
        return "<cannot unpack message>";
    } else {
        return os.str();
    }
}

td::Result<td::UInt256> get_uint256(std::string str) {
    if (str.size() != 64) {
        return td::Status::Error("uint256 must have 64 bytes");
    }
    td::UInt256 res;
    for (size_t i = 0; i < 32; i++) {
        res.raw[i] = static_cast<td::uint8>(td::hex_to_int(str[2 * i]) * 16 + td::hex_to_int(str[2 * i + 1]));
    }
    return res;
}

td::Result<std::pair<Ref<vm::Cell>, std::shared_ptr<vm::StaticBagOfCellsDb>>> lazy_boc_deserialize(
        td::BufferSlice data) {
    vm::StaticBagOfCellsDbLazy::Options options;
    options.check_crc32c = true;
    TRY_RESULT(boc, vm::StaticBagOfCellsDbLazy::create(vm::BufferSliceBlobView::create(std::move(data)), options));
    TRY_RESULT(rc, boc->get_root_count());
    if (rc != 1) {
        return td::Status::Error(-668, "bag-of-cells is not standard (exactly one root cell expected)");
    }
    TRY_RESULT(root, boc->get_root_cell(0));
    return std::make_pair(std::move(root), std::move(boc));
}