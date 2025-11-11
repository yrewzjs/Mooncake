import re
from collections import defaultdict

def process_files(read_file, write_file):
    """
    处理两个文件，统计四种操作的平均时间
    """
    # 初始化数据结构存储所有操作的时间记录
    operations_data = {
        'batch_put_from_multi_buffers': defaultdict(list),
        'batch_get_into_multi_buffers': defaultdict(list)
    }

    # 第二种格式的正则表达式: I20251027 20:55:24.025612 36525 ... batch_put_from_multi_buffers: 928us
    pattern_put = r'I\d{8} \d{2}:\d{2}:\d{2}\.\d+ (\d+) .*batch_put_from_multi_buffers:\s*(\d+)us'
    pattern_get = r'I\d{8} \d{2}:\d{2}:\d{2}\.\d+ (\d+) .*batch_get_into_multi_buffers:\s*(\d+)us'
    
    # 处理第一个文件
    try:
        with open(write_file, 'r', encoding='utf-8') as file:
            for line in file:
                line = line.strip()
                
                # 尝试匹配第二种格式 (batch_put_from_multi_buffers)
                match = re.search(pattern_put, line)
                if match:
                    pid = int(match.group(1))
                    time_value = float(match.group(2))
                    operations_data['batch_put_from_multi_buffers'][pid].append(time_value)
                    continue
    except FileNotFoundError:
        print(f"错误：文件 {write_file} 未找到")
        return None
    except Exception as e:
        print(f"读取文件 {write_file} 时出错：{e}")
        return None
    
    # 处理第二个文件
    try:
        with open(read_file, 'r', encoding='utf-8') as file:
            for line in file:
                line = line.strip()
                
                # 尝试匹配第二种格式 (batch_get_into_multi_buffers)
                match = re.search(pattern_get, line)
                if match:
                    pid = int(match.group(1))
                    time_value = float(match.group(2))
                    operations_data['batch_get_into_multi_buffers'][pid].append(time_value)
                    continue
    except FileNotFoundError:
        print(f"错误：文件 {read_file} 未找到")
        return None
    except Exception as e:
        print(f"读取文件 {read_file} 时出错：{e}")
        return None
    
    # 计算每个操作的平均时间（排除每个pid最大的10个时间记录）
    results = {}
    
    for op_name, pid_records in operations_data.items():
        # 收集有效时间（排除每个pid最大的10个时间记录）
        valid_times = []
        pid_stats = {}
        excluded_stats = {
            'total_excluded': 0,
            'max_excluded': 0,
            'min_excluded': float('inf')
        }
        
        for pid, times in pid_records.items():
            total_records = len(times)
            if total_records > 10:
                # 排序并排除最大的10个
                sorted_times = sorted(times)  # 升序排列
                # 排除最大的10个，取其余的值
                valid_records = sorted_times[:-10]
                excluded_for_pid = sorted_times[-10:]  # 记录被排除的值
                
                valid_times.extend(valid_records)
                
                # 更新排除统计
                excluded_stats['total_excluded'] += len(excluded_for_pid)
                if excluded_for_pid:
                    excluded_stats['max_excluded'] = max(excluded_stats['max_excluded'], max(excluded_for_pid))
                    excluded_stats['min_excluded'] = min(excluded_stats['min_excluded'], min(excluded_for_pid))
                
                pid_stats[pid] = {
                    'total': total_records,
                    'valid': len(valid_records),
                    'excluded': 10,
                    'max_excluded': max(excluded_for_pid) if excluded_for_pid else 0,
                    'min_valid': min(valid_records) if valid_records else 0
                }
            else:
                # 如果记录数<=10，则全部排除
                excluded_for_pid = times
                excluded_stats['total_excluded'] += len(excluded_for_pid)
                if excluded_for_pid:
                    excluded_stats['max_excluded'] = max(excluded_stats['max_excluded'], max(excluded_for_pid))
                    excluded_stats['min_excluded'] = min(excluded_stats['min_excluded'], min(excluded_for_pid))
                
                pid_stats[pid] = {
                    'total': total_records,
                    'valid': 0,
                    'excluded': total_records,
                    'max_excluded': max(times) if times else 0,
                    'min_valid': 0
                }
        
        # 计算结果
        if valid_times:
            average_time = sum(valid_times) / len(valid_times)
            min_time = min(valid_times)
            max_time = max(valid_times)
            
            results[op_name] = {
                'average': average_time,
                'min': min_time,
                'max': max_time,
                'total_valid': len(valid_times),
                'pid_stats': pid_stats,
                'excluded_stats': excluded_stats
            }
        else:
            results[op_name] = {
                'average': 0,
                'min': 0,
                'max': 0,
                'total_valid': 0,
                'pid_stats': pid_stats,
                'excluded_stats': excluded_stats
            }
    
    return results

def print_statistics(results):
    """
    打印统计结果
    """
    print("=" * 80)
    print("操作时间统计报告 (排除每个PID最大的10个时间记录)")
    print("=" * 80)
    
    # 定义操作显示顺序
    operations_order = [
        'batch_put_from_multi_buffers',
        'batch_get_into_multi_buffers'
    ]
    
    for op_name in operations_order:
        if op_name not in results:
            continue
            
        result = results[op_name]
        print(f"\n{op_name.upper()} 统计:")
        print("-" * 60)
        
        if result['total_valid'] > 0:
            print(f"平均时间：{result['average']:.2f} μs")
            print(f"最小时间：{result['min']:.2f} μs")
            print(f"最大时间：{result['max']:.2f} μs")
            print(f"有效记录总数：{result['total_valid']}")
            
            # 排除统计
            print(f"排除记录总数：{result['excluded_stats']['total_excluded']}")
            if result['excluded_stats']['total_excluded'] > 0:
                print(f"排除的最小时间：{result['excluded_stats']['min_excluded']:.2f} μs")
                print(f"排除的最大时间：{result['excluded_stats']['max_excluded']:.2f} μs")
            
            # 打印PID统计摘要
            total_pids = len(result['pid_stats'])
            pids_with_data = sum(1 for stats in result['pid_stats'].values() if stats['valid'] > 0)
            print(f"总PID数量：{total_pids}（其中 {pids_with_data} 个有有效数据）")
            
            # 显示前几个PID的详细情况
            print("\n各PID统计（前5个）：")
            count = 0
            for pid, stats in result['pid_stats'].items():
                if count >= 5:
                    remaining = len(result['pid_stats']) - 5
                    print(f"  ... 还有{remaining}个PID")
                    break
                status = "有数据" if stats['valid'] > 0 else "无有效数据"
                if stats['valid'] > 0:
                    print(f"  PID {pid}: 总记录{stats['total']}条, 排除{stats['excluded']}条, 有效{stats['valid']}条 - {status}")
                    print(f"        排除的最大值: {stats['max_excluded']:.2f} μs, 有效最小值: {stats['min_valid']:.2f} μs")
                else:
                    print(f"  PID {pid}: 总记录{stats['total']}条, 排除{stats['excluded']}条, 有效{stats['valid']}条 - {status}")
                count += 1
        else:
            print("没有找到有效的数据记录")

# 使用示例
if __name__ == "__main__":
    read_file = "read.log"   # 包含 batch_put_from_multi_buffers
    write_file = "write.log"  # 包含 batch_get_into_multi_buffers
    
    results = process_files(read_file, write_file)
    if results:
        print_statistics(results)