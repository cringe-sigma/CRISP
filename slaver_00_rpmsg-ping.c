
/***************************** Include Files *********************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <openamp/open_amp.h>
#include <metal/alloc.h>
#include <metal/device.h>
#include "fpsci.h"
#include "fdebug.h"
#include "fsleep.h"
#include "platform_info.h"
#include "helper.h"
#include "rpmsg_service.h"
#include "openamp_configs.h"
#include "rsc_table.h"
#include "libmetal_configs.h"
#include "slaver_00_example.h"

#include "inc.h"
/************************** Constant Definitions *****************************/

/***************** Macros (Inline Functions) Definitions *********************/
#define     SLAVER_00_DEBUG_TAG "    SLAVER_00"
#define     SLAVER_00_DEBUG_I(format, ...) FT_DEBUG_PRINT_I( SLAVER_00_DEBUG_TAG, format, ##__VA_ARGS__)
#define     SLAVER_00_DEBUG_W(format, ...) FT_DEBUG_PRINT_W( SLAVER_00_DEBUG_TAG, format, ##__VA_ARGS__)
#define     SLAVER_00_DEBUG_E(format, ...) FT_DEBUG_PRINT_E( SLAVER_00_DEBUG_TAG, format, ##__VA_ARGS__)
/**************************** Type Definitions *******************************/

#define PAYLOAD_MIN_SIZE            1
/*Accacker type*/
#define CACHE
//#define BUS
//#define POINTER
//#define MEM
//#define PIPELINE


/************************** Variable Definitions *****************************/
struct _payload
{
    unsigned long num;
    unsigned long size;
    unsigned char data[];
};

/* Globals */
static int ept_deleted = 0;
int syn_flag = 0; // syn

static struct remote_resource_table __resource resources __attribute__((used)) = {
	/* Version */
	1,

	/* NUmber of table entries */
	NUM_TABLE_ENTRIES,
	/* reserved fields */
	{0, 0,},

	/* Offsets of rsc entries */
	{
	 offsetof(struct remote_resource_table, rpmsg_vdev),
	 },

	/* Virtio device entry */
	{
	 RSC_VDEV, VIRTIO_ID_RPMSG_, VDEV_NOTIFYID, RPMSG_IPU_C0_FEATURES, 0, 0, 0,
	 NUM_VRINGS, {0, 0},
	 },

	/* Vring rsc entry - part of vdev rsc entry */
	{SLAVE00_TX_VRING_ADDR, VRING_ALIGN, SLAVE00_VRING_NUM, 1, 0},
	{SLAVE00_RX_VRING_ADDR, VRING_ALIGN, SLAVE00_VRING_NUM, 2, 0},
};

struct metal_device kick_driver_00 = {
    .name = SLAVE_00_KICK_DEV_NAME,
	.bus = NULL,
    .irq_num = 1,/* Number of IRQs per device */
	.irq_info = (void *)SLAVE_00_SGI,
};
/*描述remote proc实例的私有数据结构*/
struct remoteproc_priv slave_00_priv = {
    .kick_dev_name =           SLAVE_00_KICK_DEV_NAME  ,
	.kick_dev_bus_name =        KICK_BUS_NAME ,
    .cpu_id        = MASTER_CORE ,/* CPU ID to run remoteproc instance */

	.src_table_attribute = SLAVE00_SOURCE_TABLE_ATTRIBUTE ,

	/* |rx vring|tx vring|share buffer| */
	.share_mem_va = SLAVE00_SHARE_MEM_ADDR ,
	.share_mem_pa = SLAVE00_SHARE_MEM_ADDR ,
	.share_buffer_offset = SLAVE00_VRING_SIZE ,
	.share_mem_size = SLAVE00_SHARE_MEM_SIZE ,
	.share_mem_attribute = SLAVE00_SHARE_BUFFER_ATTRIBUTE
} ;

struct remoteproc remoteproc_slave_00 ;
static struct rpmsg_device *rpdev_slave_00 = NULL;
struct rpmsg_endpoint ept_slave_00 ;

/************************** Function Prototypes ******************************/

/*-----------------------------------------------------------------------------*
 *  RPMSG endpoint callbacks
 *-----------------------------------------------------------------------------*/
/* 定义传输数据结构 */
struct parameters {
    int param[5];
};



int val1 = -1, val2 = -1, val3 = -1, val4 = -1;
static int rpmsg_endpoint_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
                             uint32_t src, void *priv)
{
      /* 1. 转换数据指针 */
    struct parameters *rx_params = (struct parameters *)data;

    /* 2. 提取参数 */
    val1 = rx_params->param[0];
    val2 = rx_params->param[1];
    val3 = rx_params->param[2];
    val4 = rx_params->param[3];

    /* 3. 示例处理（实际应转存到安全位置）*/
//    SLAVER_00_DEBUG_I("Received params: %d, %d, %d, %d", val1, val2, val3, val4);

     /* 发送确认消息给主核 */

    syn_flag = 1;

    return RPMSG_SUCCESS;
}

static void rpmsg_service_unbind(struct rpmsg_endpoint *ept)
{
    (void)ept;
    rpmsg_destroy_ept(&ept_slave_00);
    SLAVER_00_DEBUG_I("Echo test: service is destroyed.\r\n");
    ept_deleted = 1;

}

_UNUSED static void rpmsg_name_service_bind_cb(struct rpmsg_device *rdev,
                                       const char *name, uint32_t dest)
{
    SLAVER_00_DEBUG_I("New endpoint notification is received.\r\n");
    if (strcmp(name, RPMSG_SERVICE_00_NAME))
    {
        SLAVER_00_DEBUG_E("Unexpected name service %s.\r\n", name);
    }
    else
        (void)rpmsg_create_ept(&ept_slave_00, rdev, RPMSG_SERVICE_00_NAME,
                               SLAVE_DEVICE_00_EPT_ADDR, dest,
                               rpmsg_endpoint_cb,
                               rpmsg_service_unbind);
}


int slave_init(void)
{
    init_system();  // Initialize the system resources and environment

    if (!platform_create_proc(&remoteproc_slave_00, &slave_00_priv, &kick_driver_00))
    {
        SLAVER_00_DEBUG_E("Failed to create remoteproc instance for slave 00\r\n");
        return -1;  // Return with an error if creation fails
    }

    remoteproc_slave_00.rsc_table = &resources;

    if (platform_setup_src_table(&remoteproc_slave_00,remoteproc_slave_00.rsc_table))
    {
        SLAVER_00_DEBUG_E("Failed to setup src table for slave 00\r\n");
        return -1;  // Return with an error if setup fails
    }

    SLAVER_00_DEBUG_I("Setup resource tables for the created remoteproc instances is over \r\n");

    if (platform_setup_share_mems(&remoteproc_slave_00))
    {
        SLAVER_00_DEBUG_E("Failed to setup shared memory for slave 00\r\n");
        return -1;  // Return with an error if setup fails
    }

    SLAVER_00_DEBUG_I("Setup shared memory regions for both remoteproc instances is over \r\n");

    rpdev_slave_00 = platform_create_rpmsg_vdev(&remoteproc_slave_00, 0, VIRTIO_DEV_SLAVE, NULL, NULL);
    if (!rpdev_slave_00)
    {
        SLAVER_00_DEBUG_E("Failed to create rpmsg vdev for slave 00\r\n");
        return -1;  // Return with an error if creation fails
    }

    return 0 ;
}

/**
 * @file    main.c
 * @brief   test
 * @details mixer cache template coding
 * @author  gejiahao
 * @version 1.0.0
 * @date    25/00/00
 * @license AAL
 */

/* constant */
#define KB ((1) << 10)
#define MB (((1LL) << 10) << 10)
#define CACHE_LINE_SIZE 16
#define L1D_CACHE_SIZE
#define L1I_CACHE_SIZE
#define L2_CACHE_SIZE
#define L3_CACHE_SIZE
/* type */

#define INFINITE /* infinite for attacker*/

#ifdef CACHE
enum oper_type
{
    Read,
    Write
};
typedef enum oper_type OPER;

#define ITER 1000 /* number of iteration  */
#define ARR_SIZE (MEM_SIZE / sizeof(int32_t))

/* var */
#define MEM_SIZE  ((factor) * (KB))
#define STRIDE (strides)
#define RW_RATIO  (ratio1)/* read-write ratio*/
#define FR_RATIO  (ratio2)/* forward-reverse ratio*/

/**
 * @brief proportional scaling
 * @param - size - array size
 *          ratio - one operation ratio
 * @version 1.0.0
 * @date 25/00/00
 * @return number of occupied elements
 */
int64_t scale(int64_t size, uint8_t ratio)
{
    switch(ratio%25)
    {
        case 0: /*0,25,50,75,100*/
            return (ratio / 25) * size / 4;
        case 5: /*5,30,55,80*/
        case 10: /*10,35,60,85*/
        case 15: /*15,40,65,90*/
        case 20: /*20,45,70,95*/
            return ratio / 5 * size / 20;
        default: /*number not divisible by 5*/
            return ratio * size  / 100;
    }
}

/* storage operation types and indexes */
typedef struct
{
    OPER type;/* 0 for read operation, 1 for write operation */
    int index;
}Operation;

/**
 * @brief generating sequences of read and write operations
 * @param - ops - sequences of operations
 *          size - array size
 *          ratio - read opertion ratio
 * @version 1.0.0
 * @date 25/00/00
 * @return 0 - for success
 *         else - for exceptions
 */
void generateOperations(Operation * ops, int64_t size, uint8_t ratio)
{
    int64_t readCount = scale(size, ratio);
    /* init sequences of operations */
    for (int i = 0; i < readCount; i++)
    {
        ops[i].type = Read;
        ops[i].index = i;
    }
    for (int i = readCount; i < size; i++)
    {
        ops[i].type = Write;
        ops[i].index = i;
    }

    /* randomly disrupt the sequence of operations */
    srand(time(NULL));
    for (int i = 0; i < size; i++)
    {
        int j = rand() % (size - i) + i;
        Operation temp = ops[i];
        ops[i] = ops[j];
        ops[j] = temp;
    }
}

/**
 * @brief cache stress
 * @param - factor - for mem_size
 *          strides - for stride
 *          read-write ratio
 *          forward-reverse ratio
 * @version 1.0.0
 * @date 25/00/00
 * @return 0 - for success
 *         else - for exceptions
 */

int cache(void *priv)
{
    register volatile int32_t* array = NULL;
    register uint64_t sum = 0;

    int64_t factor = val1;
    //ERROR_IF (factor < 32 || factor > 2048, "mem_size set wrong", VAR_SETTING_ERROR);
    int8_t strides = val2;
    //ERROR_IF (strides <=0 || strides > CACHE_LINE_SIZE+1, "stride set wrong", VAR_SETTING_ERROR);
    uint8_t RW_RATIO = val3;
    ERROR_IF (RW_RATIO > 100, "read-write ratio wrong", VAR_SETTING_ERROR);
    uint8_t FR_RATIO = val4;
    ERROR_IF (FR_RATIO > 100, "forward-reverse ration wrong", VAR_SETTING_ERROR);

    array = (int32_t *)malloc(MEM_SIZE);
    ERROR_IF (array == NULL, "failed to allocate memory for element!", MEMORY_ERROR);

    /* init array and oper*/
    for (int i = 0; i < ARR_SIZE; i++) array[i] = i + 1;
    int64_t fr_sep = scale(ARR_SIZE, FR_RATIO);/* forward-reverse separation point*/
    Operation* operations  = (Operation*) malloc(sizeof(Operation)*ARR_SIZE) ;
    ERROR_IF (array == NULL, "failed to allocate memory for oper!", MEMORY_ERROR);
    generateOperations(operations, ARR_SIZE, RW_RATIO);

    /*get begin event - cycle, cache_usage*/

#ifdef INFINITE /* for attacker */
    while(1)
    {
#else /* for victim */
    for (int i = 0; i < ITER; i++)
    {
#endif //INFINITE

        for (int j = 0; j < fr_sep; j += STRIDE) /* forward operation */
        {
            if (operations[j].type == Read)
            {
                sum += array[j];
            }
            else
            {
                array[j] = 0xff;
            }
        }
        for (int j = ARR_SIZE - 1; j >= fr_sep; j -= STRIDE)  /* reverse operation */
        {
            if (operations[j].type == Read)
            {
                sum += array[j];
            }
            else
            {
                array[j] = 0xff;
            }
        }

        {
            platform_poll_nonblocking(priv);
        	if(ept_deleted)
        	{
            	SLAVER_00_DEBUG_I("Power off.\r\n");
                 //释放资源后再退出
                //free((void *) operations);
                //free((void *) array);
            	FPsciCpuOff();
        	}
        }
    }

    /*get end event - cycle, cache_usage*/

    printf("sum3: %lu\n", sum); /* prevent sum from being optimised by the compiler*/
    /*output*/
	free( (void *) array);  //错误退出时释放资源
    free((void *) operations);

    return 0;
}
#endif //CACHE

#ifdef BUS
#define MEM_SIZE (SIZE*(MB))

typedef void (*CopyFunc)(void* dest, void* src, size_t count);
typedef void (*CpuOpFunc)(void* dest, void* src, size_t count);

typedef struct {
    size_t type_size;
    CopyFunc copy;
    CpuOpFunc cpu_op;
} DataTypeOps;

// 定义不同数据类型的操作函数
#define DEFINE_FUNCTIONS(type) \
void copy_##type(void* dest, void* src, size_t count) { \
    type* d = (type*)dest; \
    type* s = (type*)src; \
    for (size_t i = 0; i < count; i++) d[i] = s[i]; \
} \
void cpu_op_##type(void* dest, void* src, size_t count) { \
    type* d = (type*)dest; \
    type* s = (type*)src; \
    for (size_t i = 0; i < count; i++) d[i] = s[i] + (type)(rand() ); \
}

DEFINE_FUNCTIONS(uint8_t)
DEFINE_FUNCTIONS(int8_t)
DEFINE_FUNCTIONS(uint16_t)
DEFINE_FUNCTIONS(int16_t)
DEFINE_FUNCTIONS(uint32_t)
DEFINE_FUNCTIONS(int32_t)
DEFINE_FUNCTIONS(uint64_t)
DEFINE_FUNCTIONS(int64_t)

void init_ops(int data_type, DataTypeOps* ops) {
    switch (data_type) {
        case 1: ops->type_size=1; ops->copy=copy_uint8_t; ops->cpu_op=cpu_op_uint8_t; break;
        case 2: ops->type_size=1; ops->copy=copy_int8_t;   ops->cpu_op=cpu_op_int8_t; break;
        case 3: ops->type_size=2; ops->copy=copy_uint16_t;ops->cpu_op=cpu_op_uint16_t; break;
        case 4: ops->type_size=2; ops->copy=copy_int16_t;  ops->cpu_op=cpu_op_int16_t; break;
        case 5: ops->type_size=4; ops->copy=copy_uint32_t; ops->cpu_op=cpu_op_uint32_t; break;
        case 6: ops->type_size=4; ops->copy=copy_int32_t;  ops->cpu_op=cpu_op_int32_t; break;
        case 7: ops->type_size=8; ops->copy=copy_uint64_t; ops->cpu_op=cpu_op_uint64_t; break;
        case 8: ops->type_size=8; ops->copy=copy_int64_t;  ops->cpu_op=cpu_op_int64_t; break;
        default: SLAVER_00_DEBUG_E(stderr, "Invalid data type\n"); exit(EXIT_FAILURE);
    }
}

int bus(void *priv)
{

  	int size_mb = val1;
    int data_type = val2;
    int dir_ratio = val3;
    int cpu_ratio = val4;

    int r1 = dir_ratio/100;
    int r2 = dir_ratio%100/10;
    int r3 = dir_ratio%10;
    int total = r1+r2+r3;

    DataTypeOps ops;
    init_ops(data_type, &ops);

    // 分配内存
    size_t mem_size = (size_t)size_mb * MB;
    void* mem1 = malloc(mem_size);
    void* mem2 = malloc(mem_size);

    // 初始化内存为随机字节
    srand(time(NULL));
//    for (size_t i = 0; i < mem_size; i++) {
//        ((uint8_t*)mem1)[i] = (uint8_t)(rand() & 0xFF);
//        ((uint8_t*)mem2)[i] = (uint8_t)(rand() & 0xFF);
//    }

    size_t elements = mem_size / ops.type_size;

    while (1) {
        int dir = rand() % total;
        int use_cpu = (rand() % 100) < cpu_ratio;

        if (dir < r1) {       // mem1 -> mem2
            if (use_cpu) ops.cpu_op(mem2, mem1, elements);
            else ops.copy(mem2, mem1, elements);
        } else if (dir < r1+r2) { // mem2 -> mem1
            if (use_cpu) ops.cpu_op(mem1, mem2, elements);
            else ops.copy(mem1, mem2, elements);
        } else {              // mem1 -> mem1
            if (use_cpu) ops.cpu_op(mem1, mem1, elements);
            else ops.copy(mem1, mem1, elements);
        }

        {
            platform_poll_nonblocking(priv); //TODO : memset时间过长导致无法同步退出
        	if(ept_deleted)
        	{
            	SLAVER_00_DEBUG_I("Power off.\r\n");
                 //释放资源后再退出
            	FPsciCpuOff();
        	}
        }
    }

    free(mem1);
    free(mem2);

  	return 0;
}

#endif //BUS
#ifdef POINTER

#define PAD_CACHE_LINEPTR (CACHE_LINE_SIZE*4 - sizeof(void *))

    struct line {
    struct line *next;
    uint32_t data;  // 数据字段用于store操作
    uint8_t pad[PAD_CACHE_LINEPTR - sizeof(struct line *) - sizeof(uint32_t)];
} __attribute__((packed));

    /**
     * @brief ARMv8指针追逐基准测试
     * @param ptr 链表起始节点
     * @param load_ratio 读操作比例（0-100）
     */
    long benchmark_armv8(struct line *ptr, unsigned int load_ratio) {
        register struct line *current asm("x3");
        current = ptr->next;
        unsigned int threshold = load_ratio;

        asm volatile(
        "mov w4, #0\n"               // 初始化阈值计数器
        "mov x5, #0\n"               // 初始化总循环次数计数器
        "1:\n"
        "cmp w4, %w[threshold]\n"    // 比较阈值
        "b.lt 2f\n"                  // 执行load分支（w4 < threshold）

        // Store操作（写入数据字段）
        "str wzr, [x3, #8]\n"
        "b 3f\n"

        // Load操作（指针追逐）
        "2:\n"
        "ldr x3, [x3]\n"             // 更新current指针

        // 更新计数器并检查循环条件
        "3:\n"
        "add w4, w4, #1\n"           // 阈值计数器加1
        "cmp w4, #5\n"             // 检查是否到100
        "csel w4, wzr, w4, eq\n"     // 每100次重置阈值计数器
        "add x5, x5, #1\n"           // 总循环次数加1
        "cmp x5, #1\n"            // 检查是否达到1次
        "b.lt 1b\n"                  // 未达到则继续循环

        :
        : [threshold] "r" (threshold)
        : "x3", "w4", "x5", "cc", "memory"
        );

        return (long)current;
    }
    /**
     * @brief 创建内存缓冲区并启动测试
     * @param elements 链表元素数量
     * @param stride 步长
     * @param load_ratio 读操作比例（0-100）
     */
    int launch(unsigned long elements, unsigned long stride, unsigned int load_ratio,void *priv) {
        struct line *mem_chunk = calloc(elements, sizeof(struct line));
        if (!mem_chunk) {
            perror("Failed to allocate memory");
            return -1;
        }

        // 初始化链表结构
        for (unsigned long j = 0; j < elements; j++) {
            mem_chunk[j].next = &mem_chunk[(j + stride) % elements];
        }
        while(1)
          {
//            benchmark_armv8(mem_chunk, load_ratio,1);
//            {
//                platform_poll_nonblocking(priv); //TODO : 时间过长导致无法同步退出
//                if(ept_deleted)
//                {
//                    SLAVER_00_DEBUG_I("Power off.\r\n");
//                    //释放资源后再退出
//                    FPsciCpuOff();
//                }
//            }
          }


        free(mem_chunk);
        return 0;
    }


int pointer(void *priv)
{
    unsigned long elements = val1*10000;
    unsigned long stride = val2;
    unsigned int load_ratio = val3;

    return launch(elements, stride, load_ratio,priv);
}
#endif //POINTER

#ifdef MEM

typedef void (*memset_func)(void *mem, size_t offset, size_t page_size);

// 定义四种内存操作函数
void memset_normal(void *mem, size_t offset, size_t page_size) {
    memset((void *)mem + offset, rand(), page_size);
}

void memset_half_page(void *mem, size_t offset, size_t page_size) {
    memset((void *)mem + offset, rand(), page_size / 2);
}

void memset_half_offset(void *mem, size_t offset, size_t page_size) {
    memset((void *)mem + offset / 2, rand(), page_size / 2);
}

void memset_half(void *mem, size_t offset, size_t page_size) {
    memset((void *)mem + offset / 2, rand(), page_size / 2);
}

int mem(void *priv)
{

    //SLAVER_00_DEBUG_I("TEST!!!");
    int size_mb = val1;
    int page_size = val2;
    int op_ratio = val3;
    int temp = val4;

    int sum = 0;
    int percentages[4];
    for (int i = 3; i >=0; i--)
    {
       percentages[i] = op_ratio%10*10;
       op_ratio = (op_ratio/10);
       sum+= (percentages[i]/10);
    }
    //SLAVER_00_DEBUG_I("sum::%d",sum);
    memset_func func_table[100];
    const memset_func funcs[4] = {
        memset_normal, memset_half_page, memset_half_offset, memset_half
    };

    int index = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < percentages[i]; j++) {
            if (index >= 100) break;
            func_table[index++] = funcs[i];
        }
    }

    // 分配内存
    size_t mem_size = (size_t)size_mb * MB;
    size_t chunks = mem_size / page_size;
    //SLAVER_00_DEBUG_I("mem_size::%d,CHUNKS::%d,page::%d\r\n",mem_size,chunks,page_size);
    volatile void *mem = malloc(mem_size);
    if (!mem) {
        printf("Memory allocation failed");
        return ;
    }
    srand(time(NULL));

    while (1) {

        size_t chunk = rand() % chunks;
        size_t offset = chunk * page_size;

        // 每次循环执行5次随机内存操作
        for (int i = 0; i < 5; i++) {
            int r = rand() % 100;
            func_table[r]((void *)mem, offset, page_size);

         {
            platform_poll_nonblocking(priv);
        	if(ept_deleted)
        	{
            	SLAVER_00_DEBUG_I("Power off.\r\n");
                 //释放资源后再退出
            	FPsciCpuOff();
        	}
        }

        }
    }

    free((void *)mem);
    return 0;
}
#endif //MEM

#ifdef PIPELINE
static double const a0 = +1.0;
static double const a1 = -1.666666666666580809419428987894207e-1;
static double const a2 = +8.333333333262716094425007738346873e-3;
static double const a3 = -1.984126982005911439283646346964929e-4;
static double const a4 = +2.755731607338689220657382272783309e-6;
static double const a5 = -2.505185130214293595900283000271652e-8;
static double const a6 = +1.604729591825977400374002000065495e-10;
static double const a7 = -7.364589573262279913270651228486670e-13;

double A0, A1, A2, A3, A4, A5, A6, A7;

static unsigned xorshift_state;

typedef double (*operation)(double x);
operation ops[2];

static inline unsigned xorshift() {
    xorshift_state ^= xorshift_state << 13;
    xorshift_state ^= xorshift_state >> 17;
    xorshift_state ^= xorshift_state << 5;
    return xorshift_state;
}

// 手动实现10^n
static int power10(int n) {
    int result = 1;
    while(n-- > 0) result *= 10;
    return result;
}

    // 截断小数到指定精度
static double truncate_precision(double value, int precision) {
    const int factor = power10(precision);
    return (double)((long long)(value * factor)) / factor;
}

double sin_full_pipeline(double x) {
    return x * A0
         + x * x * x * A1
         + x * x * x * x * x * A2
         + x * x * x * x * x * x * x * A3
         + x * x * x * x * x * x * x * x * x * A4
         + x * x * x * x * x * x * x * x * x * x * x * A5
         + x * x * x * x * x * x * x * x * x * x * x * x * x * A6
         + x * x * x * x * x * x * x * x * x * x * x * x * x * x * x * A7;
}

double sin_buble_pipeline(double x) {
    const double x2 = x * x;
    return x * (A0 + x2 * (A1 + x2 * (A2 + x2 * (A3 + x2 *
           (A4 + x2 * (A5 + x2 * (A6 + x2 * A7)))))));
}

int pipeline(void *priv)
{
    int ratio = val1;
    int precision = val2;

    A0 = truncate_precision(a0, precision);
    A1 = truncate_precision(a1, precision);
    A2 = truncate_precision(a2, precision);
    A3 = truncate_precision(a3, precision);
    A4 = truncate_precision(a4, precision);
    A5 = truncate_precision(a5, precision);
    A6 = truncate_precision(a6, precision);
    A7 = truncate_precision(a7, precision);
    srand(time(NULL));
    xorshift_state = time(NULL);
    double sum = 0.0;
    unsigned rand_val;
    double x;


    while(1)
    {
        // 生成决策随机数（0-99）
        rand_val = xorshift();
        const int decision = rand_val % 100;

        x = rand();

        // 无分支选择计算方式
        sum += (decision < ratio) ?
            sin_full_pipeline(x) :
            sin_buble_pipeline(x);

        // 防止编译器优化
        asm volatile("" : "+g"(sum));

        {
            platform_poll_nonblocking(priv);
            if(ept_deleted)
            {
                SLAVER_00_DEBUG_I("Power off.\r\n");
                //释放资源后再退出
                FPsciCpuOff();
            }
        }
    }
    printf("sum3: %lu\n", sum); /* prevent sum from being optimised by the compiler*/
   return 0;
}
#endif //PIPELINE

#include <stdio.h>


/*-----------------------------------------------------------------------------*
 *  Application
 *-----------------------------------------------------------------------------*/

static int app(struct rpmsg_device *rdev, void *priv)
{
    int ret;
    int i;

    ept_deleted = 0;
	syn_flag = 0;
    /* Create RPMsg endpoint */
    ret = rpmsg_create_ept(&ept_slave_00, rdev, RPMSG_SERVICE_00_NAME,
                           SLAVE_DEVICE_00_EPT_ADDR, MASTER_DRIVER_EPT_ADDR,
                           rpmsg_endpoint_cb, rpmsg_service_unbind);

    if (ret)
    {
        SLAVER_00_DEBUG_E("Failed to create RPMsg endpoint.\r\n");
        return ret;
    }
    while (!is_rpmsg_ept_ready(&ept_slave_00))
    {
        SLAVER_00_DEBUG_I("start to wait platform_poll \r\n");
        platform_poll(priv);
    }
    SLAVER_00_DEBUG_I("**********************************\r\n");
    SLAVER_00_DEBUG_I("RPMSG endpoint is binded with remote.\r\n");
    SLAVER_00_DEBUG_I("**********************************\r\n");

    while(!syn_flag)
    {
        platform_poll(priv);
    }
	SLAVER_00_DEBUG_I("set arg suceess\r\n");

    SLAVER_00_DEBUG_I("Slave 00 start run!!!\r\n");

    int error = 0;
#ifdef CACHE
    error = cache(priv);
#elif defined(BUS)
    error = bus(priv);
#elif defined(POINTER)
    error = pointer(priv);
#elif defined(MEM)
    error = mem(priv);
#elif defined(PIPELINE)
    error = pipeline(priv);
#else
    SLAVER_00_DEBUG_I("Invalid Attacker Type!!\r\n");
#endif
    ERROR_IF (error != 0, "failed to execute",VAR_SETTING_ERROR);

    return 0;
}


int slave00_rpmsg_echo_process()
{
    if(!slave_init())
    {
        app(rpdev_slave_00,&remoteproc_slave_00);
    }
    else
    {
        platform_cleanup(&remoteproc_slave_00);
        SLAVER_00_DEBUG_E("Failed to init remoteproc.\r\n");
    }

    return 0 ;
} 