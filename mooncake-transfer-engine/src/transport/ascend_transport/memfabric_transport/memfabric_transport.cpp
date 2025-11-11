/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "transport/ascend_transport/memfabric_transport/memfabric_transport.h"
#include "transport/ascend_transport/memfabric_transport/memfabric_api.h"

namespace mooncake {
MemFabricTransport::MemFabricTransport() {}

MemFabricTransport::~MemFabricTransport() {}

Status MemFabricTransport::submitTransfer(
    Transport::BatchID batch_id, const std::vector<TransferRequest> &entries) {
    auto count = entries.size();
    auto &batch_desc = *((BatchDesc *)(batch_id));
    if (batch_desc.task_list.size() + count > batch_desc.batch_size) {
        LOG(ERROR)
            << "MemFabricTransport: Exceed the limitation of current batch's "
               "capacity";
        return Status::InvalidArgument(
            "MemFabricTransport: Exceed the limitation of capacity, batch "
            "id: " +
            std::to_string(batch_id));
    }

    size_t task_id = batch_desc.task_list.size();
    batch_desc.task_list.resize(task_id + count);

    // TODO 一次批量任务中读写混合
    // TODO 任务来自LocalHost内存
    smem_bm_copy_type t = entries[0].opcode == TransferRequest::READ
                              ? SMEMB_COPY_G2L
                              : SMEMB_COPY_L2G;
    std::vector<void *> sources(count);
    std::vector<void *> destinations(count);
    std::vector<uint64_t> dataSizes(count);
    smem_batch_copy_params params{};
    for (auto &request : entries) {
        sources.emplace_back(request.source);
        destinations.emplace_back((void *)request.target_offset);
        dataSizes.emplace_back(request.length);
    }

    int ret = MemFabricSmemBmDl::SmemBmCopyBatch(
        MemFabricSmemBmDl::GetSmemBmHandle(), &params, t, 0);
    if (ret != 0) {
        LOG(ERROR) << "MemFabricTransport: Failed to smem bm copy batch, ret:"
                   << ret;
    }
    for (auto &request : entries) {
        TransferTask &task = batch_desc.task_list[task_id];
        ++task_id;
        task.total_bytes = request.length;
        Slice *slice = getSliceCache().allocate();
        slice->source_addr = (char *)request.source;
        slice->memfabric.dest_addr = request.target_offset;
        slice->length = request.length;
        slice->opcode = request.opcode;
        slice->task = &task;
        slice->target_id = request.target_id;
        slice->status = Slice::PENDING;
        __sync_fetch_and_add(&task.slice_count, 1);
        if (ret != 0) {
            slice->markFailed();
        } else {
            slice->markSuccess();
        }
    }
    return Status::OK();
}

Status MemFabricTransport::submitTransferTask(
    const std::vector<TransferTask *> &task_list) {
    uint32_t count = task_list.size();

    // TODO 一次批量任务中读写混合
    // TODO 任务来自LocalHost内存
    smem_bm_copy_type t = task_list[0]->request->opcode == TransferRequest::READ
                              ? SMEMB_COPY_G2L
                              : SMEMB_COPY_L2G;
    std::vector<void *> sources(count);
    std::vector<void *> destinations(count);
    std::vector<uint64_t> dataSizes(count);
    for (size_t index = 0; index < task_list.size(); ++index) {
        auto &task = *task_list[index];
        auto &request = *task.request;
        sources[index] = request.opcode == TransferRequest::READ
                             ? reinterpret_cast<void *>(request.target_offset)
                             : request.source;
        destinations[index] =
            request.opcode == TransferRequest::READ
                ? request.source
                : reinterpret_cast<void *>(request.target_offset);
        dataSizes[index] = request.length;
    }
    smem_batch_copy_params params = {sources.data(), destinations.data(),
                                     dataSizes.data(), count};
    int ret = MemFabricSmemBmDl::SmemBmCopyBatch(
        MemFabricSmemBmDl::GetSmemBmHandle(), &params, t, 0);
    if (ret != 0) {
        LOG(ERROR) << "MemFabricTransport: Failed to smem bm copy batch, ret:"
                   << ret;
    }
    for (size_t index = 0; index < task_list.size(); ++index) {
        auto &task = *task_list[index];
        auto &request = *task.request;
        task.total_bytes = request.length;
        Slice *slice = getSliceCache().allocate();
        slice->source_addr = (char *)request.source;
        slice->memfabric.dest_addr = request.target_offset;
        slice->length = request.length;
        slice->opcode = request.opcode;
        slice->task = &task;
        slice->target_id = request.target_id;
        slice->status = Slice::PENDING;
        __sync_fetch_and_add(&task.slice_count, 1);
        if (ret != 0) {
            slice->markFailed();
        } else {
            slice->markSuccess();
        }
    }
    return Status::OK();
}

Status MemFabricTransport::getTransferStatus(
    Transport::BatchID batch_id, size_t task_id,
    Transport::TransferStatus &status) {
    auto &batch_desc = *((BatchDesc *)(batch_id));
    const size_t task_count = batch_desc.task_list.size();
    if (task_id >= task_count) {
        return Status::InvalidArgument(
            "MemFabricTransport::getTransportStatus invalid argument, batch "
            "id: " +
            std::to_string(batch_id));
    }
    auto &task = batch_desc.task_list[task_id];
    status.transferred_bytes = task.transferred_bytes;
    uint64_t success_slice_count = task.success_slice_count;
    uint64_t failed_slice_count = task.failed_slice_count;
    if (success_slice_count + failed_slice_count == task.slice_count) {
        if (failed_slice_count) {
            status.s = TransferStatusEnum::FAILED;
        } else {
            status.s = TransferStatusEnum::COMPLETED;
        }
        task.is_finished = true;
    } else {
        status.s = TransferStatusEnum::WAITING;
    }
    return Status::OK();
}

int MemFabricTransport::install(std::string &local_server_name,
                                std::shared_ptr<TransferMetadata> meta,
                                std::shared_ptr<Topology> topo) {
    metadata_ = meta;
    local_server_name_ = local_server_name;

    auto desc = std::make_shared<SegmentDesc>();
    if (!desc) return ERR_MEMORY;
    desc->name = local_server_name_;
    desc->protocol = "memfabric";
    metadata_->addLocalSegment(LOCAL_SEGMENT_ID, local_server_name_,
                               std::move(desc));
    return 0;
}

int MemFabricTransport::registerLocalMemory(void *addr, size_t length,
                                            const std::string &location,
                                            bool remote_accessible,
                                            bool update_metadata) {
    return 0;
}

int MemFabricTransport::unregisterLocalMemory(void *addr,
                                              bool update_metadata) {
    return 0;
}

int MemFabricTransport::registerLocalMemoryBatch(
    const std::vector<Transport::BufferEntry> &buffer_list,
    const std::string &location) {
    return 0;
}

int MemFabricTransport::unregisterLocalMemoryBatch(
    const std::vector<void *> &addr_list) {
    return 0;
}
}  // namespace mooncake
