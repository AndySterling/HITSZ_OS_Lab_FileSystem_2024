#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define MAX_NAME_LEN    128
#define TRUE            1
#define FALSE           0  
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_MAGIC_NUM           0x22011022 
#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0

#define NFS_ERROR_NONE          0
#define NFS_ERROR_ACCESS        EACCES
#define NFS_ERROR_SEEK          ESPIPE     
#define NFS_ERROR_ISDIR         EISDIR
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ERROR_EXISTS        EEXIST
#define NFS_ERROR_NOTFOUND      ENOENT
#define NFS_ERROR_UNSUPPORTED   ENXIO
#define NFS_ERROR_IO            EIO     /* Error Input/Output */
#define NFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NFS_INODE_PER_FILE      1
#define NFS_DATA_PER_FILE       6
#define NFS_DEFAULT_PERM        0777

#define NFS_IOC_MAGIC           'S'
#define NFS_IOC_SEEK            _IO(NFS_IOC_MAGIC, 0)

#define NFS_FLAG_BUF_DIRTY      0x1
#define NFS_FLAG_BUF_OCCUPY     0x2

// 磁盘布局
// sizeof(struct nfs_inode) = 104
// 设计一个逻辑块存储8个索引节点，一个文件最多直接索引6个逻辑块来填写文件数据，则8个文件需要的存储容量是8 * 6 + 1 = 49KB
// 4MB的磁盘最多可以存放的文件个数是(4MB / 49KB) * 8 = 664个文件，需要4MB / 49KB = 83个逻辑块存储索引节点
#define NFS_SUPER_BLOCK_NUM     1   // 超级块占用1个逻辑块
#define NFS_INODE_MAP_BLOCK_NUM 1   // 索引节点位图占用1个逻辑块
#define NFS_DATA_MAP_BLOCK_NUM  1   // 数据块位图占用1个逻辑块
#define NFS_INODE_BLOCK_NUM     83   // 需要83个逻辑块存储索引节点
#define NFS_DATA_BLOCK_NUM      4010   // 还剩下4096 - 1 - 1 - 1 - 83 = 4010个逻辑块作为数据块
/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef enum nfs_file_type {
    NFS_REG_FILE,
    NFS_DIR,
    // NFS_SYM_LINK   // 实验无需考虑软链接和硬链接的实现
} NFS_FILE_TYPE;

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NFS_IO_SZ()                     (nfs_super.sz_io)
#define NFS_DISK_SZ()                   (nfs_super.sz_disk)
#define NFS_DRIVER()                    (nfs_super.fd)

#define NFS_BLKS_SZ(blks)               ((blks) * 2 * NFS_IO_SZ())   // 一个逻辑块的大小为2个磁盘IO大小，即1024B
#define NFS_ASSIGN_FNAME(psfs_dentry, _fname)\ 
                                        memcpy(psfs_dentry->name, _fname, strlen(_fname))

// inode索引在磁盘中的偏移量(大小为nfs_inode_d，因为只有从磁盘中读和往磁盘中写时用到该函数) (修改逻辑块内可存放的inode_d数量只需修改NFS_INO_OFS和NFS_INODE_BLOCK_NUM)
#define NFS_INO_OFS(ino)                (nfs_super.inode_offset + NFS_BLKS_SZ(ino / 8) + (ino % 8) * sizeof(struct nfs_inode_d))
// 数据块起始地址  
#define NFS_DATA_OFS(ino)               (nfs_super.data_offset + NFS_BLKS_SZ(ino))                             

#define NFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))   // 不超过value中round的最大倍数
#define NFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))   // 不小于value中round的最小倍数

#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == NFS_DIR)
#define NFS_IS_REG(pinode)              (pinode->dentry->ftype == NFS_REG_FILE)
/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct nfs_dentry;
struct nfs_inode;
struct nfs_super;
struct nfs_inode_d;

struct custom_options {
	const char*        device;
};

struct nfs_super {
    uint32_t magic;
    int      fd;
    /* TODO: Define yourself */
    int sz_io;   // io大小
    int sz_disk;   // 磁盘容量大小
    int sz_blks;   // 磁盘逻辑块大小，为1024B
    int sz_usage;

    int max_ino;   // inode数目
    uint8_t* map_inode;   // inode位图
    int map_inode_blks;   // inode位图所占的数据块
    int map_inode_offset;   // inode位图的起始地址

    int max_data;   // 数据块数目
    uint8_t* map_data;   // 数据块位图
    int map_data_blks;   // 数据块位图所占的数据块
    int map_data_offset;   // 数据块位图的起始地址

    int inode_offset;   // 索引节点块的起始地址
    int data_offset;   // 数据块的起始地址

    boolean is_mounted;

    struct nfs_dentry* root_dentry;   // 根目录

};

struct nfs_inode {
    uint32_t ino;   // 在inode位图中的下标
    /* TODO: Define yourself */
    int size;   // 文件已占用空间大小
    int  dir_cnt;   // 目录项个数
    struct nfs_dentry* dentry;    // 指向该inode的dentry
    struct nfs_dentry* dentrys;   // 所有目录项
    int block_num;   // 已分配数据块数量
    int block_index[6];   // 数据块在磁盘中的块号 
    uint8_t* block_pointer[6];   // 数据块指针(假设每个文件最多直接索引6个逻辑块来填写文件数据)
};

struct nfs_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
    struct nfs_dentry* parent;   // 父亲Inode的dentry 
    struct nfs_dentry* brother;   // 兄弟 
    struct nfs_inode*  inode;   // 指向inode
    NFS_FILE_TYPE      ftype;
};

// 函数功能：创建目录项
static inline struct nfs_dentry* new_dentry(char * fname, NFS_FILE_TYPE ftype) {
    struct nfs_dentry * dentry = (struct nfs_dentry *)malloc(sizeof(struct nfs_dentry));
    memset(dentry, 0, sizeof(struct nfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL; 
    return dentry;                                            
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct nfs_super_d{
    uint32_t magic_num;
    uint32_t sz_usage;

    int max_ino;   // inode数目
    int map_inode_offset;   // inode位图的起始地址
    int map_inode_blks;   // inode位图所占的数据块

    int max_data;   // 数据块数目
    int map_data_blks;   // 数据块位图所占的数据块
    int map_data_offset;   // 数据块位图的起始地址

    int inode_offset;   // 索引节点块的起始地址
    int data_offset;   // 数据块的起始地址

};

struct nfs_inode_d{
    uint32_t ino;   // 在inode位图中的下标
    int size;   // 文件已占用空间大小
    int  dir_cnt;   // 目录项个数
    int block_num;   // 已分配数据块数量
    int block_index[6];   // 数据块在磁盘中的块号
    NFS_FILE_TYPE      ftype;   // 文件类型
};

struct nfs_dentry_d{
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    NFS_FILE_TYPE      ftype;   // 文件类型

};
#endif /* _TYPES_H_ */