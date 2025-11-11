import unittest

import torch
import torch_npu

from mooncake_store import Mooncakestore, MooncakeConfig
from test_common import tensor_sum, malloc_npu_blocks, get_col_tensors_by_index, get_col_tensors_ptr_by_index

def init_mooncake(device_id: int):
    import os
    os.environ['MF_DEVICE_ID'] = str(device_id)
    os.environ['MF_LOG_LEVEL'] = str(1)
    os.environ['MF_DRAM_SIZE'] = str(1024 * 1024 * 1024)
    os.environ['MF_OP_TYPE'] = 'device_rdma'
    config = MooncakeConfig(
        device=device_id,
        protocol='memfabric',
        device_name= '',
        local_hostname='141.61.41.87',
        metadata_server='P2PHANDSHAKE',
        global_segment_size=1024 * 1024 * 1024,
        local_buffer_size=1024 * 1024 * 1024,
        master_server_address='141.61.41.87:50051')
    store = Mooncakestore(config)
    return store

class TestExample(unittest.TestCase):
    def setUp(self):
        self.store = init_mooncake(0)

    def test_put_from_get_into(self):
        one_batch_count: int = 16
        size1 = [128 * 1024 for _ in range(61)]
        size2 = [16 * 1024 for _ in range(61)]
        block_size = [item for pair in zip(size1, size2) for item in pair]
        key_prefix: str = "key_"
        tensor1 = malloc_npu_blocks(max(size1, default=0), len(size1), one_batch_count)
        tensor2 = malloc_npu_blocks(max(size2, default=0), len(size2), one_batch_count)
        self.store.register(tensor1.data_ptr(), max(size1, default=0) * len(size1) * one_batch_count)
        self.store.register(tensor2.data_ptr(), max(size2, default=0) * len(size2) * one_batch_count)
        keys = []
        buffs = []
        sizes = []
        put_sum = []
        get_sum = []
        for j in range(one_batch_count):
            key = key_prefix + str(j)
            keys.append(key)
            block_buffs = [item for pair in zip(get_col_tensors_ptr_by_index(tensor1, len(size1), j),
                                                get_col_tensors_ptr_by_index(tensor2, len(size2), j)) for item in pair]
            buffs.append(block_buffs)
            sizes.append(block_size)
        ret = self.store.put_batch(keys, buffs, sizes)
        for j in range(one_batch_count):
            block_tensors = [item for pair in zip(get_col_tensors_by_index(tensor1, len(size1), j),
                                                  get_col_tensors_by_index(tensor2, len(size2), j)) for item in pair]
            put_sum.append(tensor_sum(block_tensors, block_size))
            self.assertEqual(ret[j], 0)

        tensor1.fill_(0)
        tensor2.fill_(0)
        torch_npu.npu.current_stream().synchronize()
        ret = self.store.get_batch(keys, buffs, sizes)
        for j in range(one_batch_count):
            block_tensors = [item for pair in zip(get_col_tensors_by_index(tensor1, len(size1), j),
                                                  get_col_tensors_by_index(tensor2, len(size2), j)) for item in pair]
            get_sum.append(tensor_sum(block_tensors, block_size))
            self.assertEqual(ret[j], max(size1, default=0) * len(size1) + max(size2, default=0) * len(size2))

        for j in range(one_batch_count):
            print(f"====key({keys[j]}) put_sum({put_sum[j]}) get_sum({get_sum[j]})")
            self.assertEqual(get_sum, put_sum)

    def tearDown(self):
        self.store.close()


if __name__ == '__main__':
    unittest.main()
