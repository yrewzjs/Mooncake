from typing import List
import torch
import torch_npu

def set_device(device_id):
    import acl
    acl.init()
    ret = acl.rt.set_device(device_id)
    if ret != 0:
        raise RuntimeError("acl set device failed")


def tensor_sum(tensor: List[torch.Tensor], sizes: List[int] = None):
    if tensor is None:
        return 0
    if sizes is None:
        return sum(layer.sum().item() for layer in tensor)
    return sum(layer[:size].sum().item() for layer, size in zip(tensor, sizes))

def allocate_aligned_tensor(shape, dtype=torch.uint8, alignment=2*1024*1024):
    num_elements = torch.prod(torch.tensor(shape)).item()
    element_size = torch.finfo(dtype).bits // 8 if dtype.is_floating_point else torch.iinfo(dtype).bits // 8
    total_bytes = num_elements * element_size

    padding = alignment - 1
    buffer = torch.randint(
        low=0, high=256,
        size=(total_bytes + padding, 1),
        dtype=dtype,
        device='npu')

    address = buffer.data_ptr()
    aligned_address = (address + alignment - 1) & ~(alignment - 1)
    offset = (aligned_address - address) // element_size

    aligned_tensor = buffer[offset:offset + num_elements].view(*shape)
    print(f"Aligned tensor address: {aligned_tensor.data_ptr():x}")
    return aligned_tensor

def malloc_npu_blocks(min_block_size: int, layer_num: int, block_num: int):
    raw_blocks = allocate_aligned_tensor((layer_num, block_num, min_block_size), torch.uint8)
    torch_npu.npu.current_stream().synchronize()
    return raw_blocks

def get_col_tensors_by_index(tensors, layer_num, block_index):
    block_tensor = []
    for li in range(layer_num):
        block_tensor.append(tensors[li][block_index])
    return block_tensor

def get_col_tensors_ptr_by_index(tensors, layer_num, block_index):
    block_ptrs = []
    for li in range(layer_num):
        block_ptrs.append(tensors[li][block_index].data_ptr())
    return block_ptrs