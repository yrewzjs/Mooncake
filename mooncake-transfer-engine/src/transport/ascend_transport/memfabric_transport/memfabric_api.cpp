#include <algorithm>
#include "transport/ascend_transport/memfabric_transport/memfabric_api.h"

namespace mooncake {

bool MemFabricSmemBmDl::gLoaded_ = false;
std::mutex MemFabricSmemBmDl::mutex_;
smem_bm_t MemFabricSmemBmDl::smemBmHandle_ = nullptr;
MemFabricConfig MemFabricSmemBmDl::config_;
FUNC_SMEM_BM_CONFIG_INIT MemFabricSmemBmDl::pSmemBmConfigInit = nullptr;
FUNC_SMEM_BM_INIT MemFabricSmemBmDl::pSmemBmInit = nullptr;
FUNC_SMEM_BM_UNINIT MemFabricSmemBmDl::pSmemBmUninit = nullptr;
FUNC_SMEM_BM_GET_RANK_ID MemFabricSmemBmDl::pSmemGetRankId = nullptr;
FUNC_SMEM_BM_CREATE MemFabricSmemBmDl::pSmemBmCreate = nullptr;
FUNC_SMEM_BM_DESTROY MemFabricSmemBmDl::pSmemBmDestory = nullptr;
FUNC_SMEM_BM_JOIN MemFabricSmemBmDl::pSmemBmJoin = nullptr;
FUNC_SMEM_BM_LEAVE MemFabricSmemBmDl::pSmemBmLeave = nullptr;
FUNC_SMEM_BM_GET_LOCAL_MEM_SIZE_BY_MEM_TYPE
MemFabricSmemBmDl::pSmemBmGetLocalMemSizeByMemType = nullptr;
FUNC_SMEM_BM_PTR_BY_MEM_TYPE MemFabricSmemBmDl::pSmemBmPtrByMemType = nullptr;
FUNC_SMEM_BM_COPY MemFabricSmemBmDl::pSmemBmCopy = nullptr;
FUNC_SMEM_BM_COPY_BATCH MemFabricSmemBmDl::pSmemBmCopyBatch = nullptr;
FUNC_SMEM_BM_WAIT MemFabricSmemBmDl::pSmemBmWait = nullptr;
FUNC_SMEM_BM_REGISTER_USER_MEM MemFabricSmemBmDl::pSmemBmRegisterUserMem =
    nullptr;
FUNC_SMEM_SET_EXTERN_LOGGER MemFabricSmemBmDl::pSmemSetExternLogger = nullptr;
FUNC_SMEM_SET_LOG_LEVEL MemFabricSmemBmDl::pSmemSetLogLevel = nullptr;
FUNC_SMEM_CREATE_CONFIG_STORE MemFabricSmemBmDl::pSmemCreateConfigStore =
    nullptr;

static void MfRegisterLogger(int level, const char* msg) {
    switch (level) {
        case 0:
            LOG(INFO) << msg;
            break;
        case 1:
            LOG(INFO) << msg;
            break;
        case 2:
            LOG(WARNING) << msg;
            break;
        case 3:
            LOG(ERROR) << msg;
            break;
        default:
            LOG(INFO) << msg;
            break;
    };
}

int InitMemFabricLog() {
    if (MemFabricSmemBmDl::SmemSetLogLevel(
            MemFabricSmemBmDl::GetMemFabricConfig().logLevel) != 0) {
        LOG(ERROR) << "mf bm set logger level failed";
        return -1;
    }

    if (MemFabricSmemBmDl::SmemSetExternLogger(MfRegisterLogger) != 0) {
        LOG(ERROR) << "mf bm set logger func failed";
        return -1;
    }
    return 0;
}

int InitMemFabricBm(const std::string& storeUrlStr) {
    std::string storeUrl;
    std::string connUrl;
    smem_bm_config_t config = {};
    if (MemFabricSmemBmDl::SmemBmConfigInit(&config) != 0) {
        LOG(ERROR) << "Failed to init smem bm config";
        return -1;
    }

    if (storeUrlStr.empty()) {
        storeUrl = MemFabricSmemBmDl::GetMemFabricConfig().storeUrl;
        config.startConfigStoreServer = true;
    } else {
        size_t colon_pos = storeUrlStr.find(":");
        if (colon_pos == std::string::npos) {
            LOG(ERROR) << "Unexpected master server addr: " << storeUrlStr;
            return -1;
        } else {
            std::string ip = storeUrlStr.substr(0, colon_pos);
            std::string portStr = storeUrlStr.substr(colon_pos + 1);
            int port = -1;
            try {
                port = std::stoi(portStr);
                port += 1;
                if (port < 1024 || port > 65534) {
                    LOG(ERROR) << "Port out of range:" << port;
                    return -1;
                }
            } catch (...) {
                LOG(ERROR) << "Error: Invalid argument, cannot convert '"
                           << portStr << "' to integer.";
                return -1;
            }
            storeUrl = "tcp://" + ip + ":" + std::to_string(port);
            connUrl = "tcp://" + ip + ":" + std::to_string(port + 1);
        }
        config.startConfigStoreServer = false;
    }
    config.flags |= SMEM_BM_INIT_GVM_FLAG;
    std::copy_n(connUrl.c_str(),
                std::min(sizeof(config.hcomUrl) - 1, connUrl.size()),
                config.hcomUrl);
    if (MemFabricSmemBmDl::SmemBmInit(
            storeUrl.c_str(), MemFabricSmemBmDl::GetMemFabricConfig().worldSize,
            MemFabricSmemBmDl::GetMemFabricConfig().deviceId, &config) != 0) {
        LOG(ERROR) << "Failed to init smem bm:" << storeUrl;
        return -1;
    }
    return 0;
}

int MemFabricInitSmemBm(std::string storeUrl) {
    if (MemFabricSmemBmDl::LoadMemFabricBmAPI() != 0) {
        LOG(ERROR) << "Error: Failed to load MemFabric api";
        return -1;
    }
    smem_bm_t handle = MemFabricSmemBmDl::GetSmemBmHandle();
    if (handle != nullptr) {
        LOG(INFO) << "mf bm already init";
        return 0;
    }
    // log
    auto ret = InitMemFabricLog();
    if (ret != 0) {
        return ret;
    }
    // init
    ret = InitMemFabricBm(storeUrl);
    if (ret != 0) {
        return ret;
    }
    // create
    uint64_t localHBMSize = MemFabricSmemBmDl::GetMemFabricConfig().hbmSize;
    uint64_t localDRAMSize = MemFabricSmemBmDl::GetMemFabricConfig().dramSize;
    smem_bm_data_op_type opType =
        MemFabricSmemBmDl::GetMemFabricConfig().opType;
    handle = MemFabricSmemBmDl::SmemBmCreate(0, 0, opType, localDRAMSize,
                                             localHBMSize, 0);
    if (handle == nullptr) {
        MemFabricSmemBmDl::SmemBmUninit(0);
        LOG(ERROR) << "Failed to create smem bm";
    }
    // join
    if (MemFabricSmemBmDl::SmemBmJoin(handle, 0) != 0) {
        MemFabricSmemBmDl::SmemBmDestory(handle);
        MemFabricSmemBmDl::SmemBmUninit(0);
        LOG(ERROR) << "Failed to join smem bm";
        return -1;
    }
    return 0;
}

int MemFabricInitConfigStore(const std::string& store_address,
                             uint32_t storePort) {
    mooncake::MemFabricSmemBmDl::LoadMemFabricBmAPI();
    if (storePort < 1024 || storePort > 65535) {
        LOG(FATAL) << "StorePort out of range:" << storePort;
        return -1;
    }
    std::string storeUrl =
        "tcp://" + store_address + ":" + std::to_string(storePort);
    int32_t ret = mooncake::MemFabricSmemBmDl::SmemSetLogLevel(1);
    if (ret != 0) {
        LOG(FATAL) << "mf set logger level failed:" << ret;
        return -1;
    }
    ret = mooncake::MemFabricSmemBmDl::SmemSetExternLogger(
        mooncake::MfRegisterLogger);
    if (ret != 0) {
        LOG(FATAL) << "mf set logger func failed:" << ret;
        return -1;
    }
    if (mooncake::MemFabricSmemBmDl::SmemCreateConfigStore(storeUrl.c_str()) !=
        0) {
        LOG(FATAL) << "mf create config store failed url:" << storeUrl;
        return -1;
    }
    return 0;
}

std::pair<void*, size_t> MemFabricGetSegment() {
    auto bmRankId = MemFabricSmemBmDl::SmemGetRankId();
    auto handle = MemFabricSmemBmDl::GetSmemBmHandle();
    void* ptr = MemFabricSmemBmDl::SmemBmPtrByMemType(
        handle, SMEM_MEM_TYPE_HOST, bmRankId);
    size_t dramSize = MemFabricSmemBmDl::SmemBmGetLocalMemSizeByMemType(
        handle, SMEM_MEM_TYPE_HOST);
    return {ptr, dramSize};
}

}  // namespace mooncake