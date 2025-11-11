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
        key = "test_key"
        mini_block_size = 1024
        layer_num = 1
        block_num = 1
        tensor = malloc_npu_blocks(mini_block_size, layer_num, block_num)
        ret = self.store.put_from(key, tensor.data_ptr(), mini_block_size)
        put_sum = tensor_sum([tensor], [mini_block_size])
        print(f"====key({key}) res({ret})) put_sum({put_sum})")
        tensor.fill_(0)
        torch_npu.npu.current_stream().synchronize()
        print(f"====tmp_sum({tensor_sum([tensor], [mini_block_size])})")
        self.store.get_into(key, tensor.data_ptr(), mini_block_size)
        get_sum = tensor_sum([tensor], [mini_block_size])
        print(f"====key({key}) res({ret})) get_sum({get_sum})")
        self.assertEqual(get_sum, put_sum)

    def tearDown(self):
        self.store.close()


if __name__ == '__main__':
    unittest.main()
