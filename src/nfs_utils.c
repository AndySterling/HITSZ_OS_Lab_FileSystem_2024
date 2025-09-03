#include "../include/nfs.h"

extern struct nfs_super      nfs_super; 
extern struct custom_options nfs_options;

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* nfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 计算路径的层级(根据“/”数量)
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int nfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 驱动读
 * 
 * @param offset：要读取的数据段在磁盘中的偏移 
 * @param out_content：存放读取出的内容 
 * @param size：读取的数据段大小 
 * @return int 
 */
int nfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLKS_SZ(1));   // 偏移所在的磁盘块的起始地址
    int      bias           = offset - offset_aligned;   // 偏移量和数据块对齐后的偏移量的差
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLKS_SZ(1));   // 读取内容需要访问数据块的大小(需访问的磁盘块数量*每个磁盘块大小)
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    // 移动磁盘头到下界down的位置
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    // 将需要访问磁盘块的内容全部读取到cur中
    while (size_aligned != 0)
    {
        // 每次读取一个IO大小
        // read(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_read(NFS_DRIVER(), (char*)cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 驱动写
 * 
 * @param offset：要写回的目标地址 
 * @param in_content：待写回的内容 
 * @param size：写回内容的大小 
 * @return int 
 */
int nfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = NFS_ROUND_DOWN(offset, NFS_BLKS_SZ(1));   // 偏移所在的磁盘块的起始地址
    int      bias           = offset - offset_aligned;   //偏移量和数据块对齐后的偏移量的差
    int      size_aligned   = NFS_ROUND_UP((size + bias), NFS_BLKS_SZ(1));   // 写回内容需要访问数据块的大小(需访问的磁盘块数量*每个磁盘块大小)
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    nfs_driver_read(offset_aligned, temp_content, size_aligned);   // 先把磁盘块所有内容读出
    memcpy(temp_content + bias, in_content, size);   // 用写回的数据替换磁盘块中对应位置的内容
    
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    // 移动磁盘头到下界down的位置
    ddriver_seek(NFS_DRIVER(), offset_aligned, SEEK_SET);
    // 将读取出的数据写回磁盘块中，每次写回一个IO的大小
    while (size_aligned != 0)
    {
        // write(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_write(NFS_DRIVER(), (char*)cur, NFS_IO_SZ());
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }

    free(temp_content);
    return NFS_ERROR_NONE;
}

/**
 * @brief 将denry插入到inode中，采用头插法
 * 
 * @param inode 父目录inode
 * @param dentry 
 * @return int 
 */
int nfs_alloc_dentry(struct nfs_inode* inode, struct nfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    inode->size += sizeof(struct nfs_dentry);   // 更新占用空间
    if(inode->dir_cnt % (NFS_BLKS_SZ(1) / sizeof(struct nfs_dentry)) == 1){
        // inode原有的数据块已满（或初始时未分配数据块）需要分配一个新的数据块
        inode->block_index[inode->block_num] = nfs_alloc_data();
        inode->block_num++;
    }
    return inode->dir_cnt;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return nfs_inode
 */
struct nfs_inode* nfs_alloc_inode(struct nfs_dentry * dentry) {
    struct nfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查inode位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                nfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);   // 将对应位置置1
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == nfs_super.max_ino)
        return (struct nfs_inode *)-NFS_ERROR_NOSPACE;

    inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    inode->block_num = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    return inode;
}

/**
 * @brief 分配一个数据块，占用位图
 * 
 * @return 分配的数据块号
 */
int nfs_alloc_data(){
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查数据位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_data_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */  
                nfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);      // 将对应位置置1
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == nfs_super.max_data)
        return -NFS_ERROR_NOSPACE;

    return ino_cursor;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int nfs_sync_inode(struct nfs_inode * inode) {
    struct nfs_inode_d  inode_d;
    struct nfs_dentry*  dentry_cursor;
    struct nfs_dentry_d dentry_d;

    // 填写inode_d相关数据
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.block_num   = inode->block_num;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    // 只刷回有效的block_index
    for(int i = 0; i < inode->block_num; i++){
        inode_d.block_index[i] = inode->block_index[i];
    }
    int offset;

    /* 先写inode本身 */
    if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return -NFS_ERROR_IO;
    }

    /* 再写inode下方的数据 */
    if (NFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */                          
        dentry_cursor = inode->dentrys;
        int i = 0;   // 循环变量，记录写回的是第几个block_index指向的数据块
        while (dentry_cursor != NULL && i <= inode->block_num)
        {
            offset        = NFS_DATA_OFS(inode->block_index[i]);
            // 循环写回所有的dentry
            while(dentry_cursor != NULL && (offset += sizeof(struct nfs_dentry_d) != NFS_DATA_OFS(inode->block_index[i] + 1))){
                // 填写dentry_d相关信息
                memcpy(dentry_d.name, dentry_cursor->name, MAX_NAME_LEN);     
                dentry_d.ftype = dentry_cursor->ftype;
                printf("dentry_d_name = %s, type = %d\n", dentry_d.name, dentry_d.ftype);
                dentry_d.ino = dentry_cursor->ino;
                if (nfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                    NFS_DBG("[%s] io error\n", __func__);
                    return -NFS_ERROR_IO;                     
                }
                
                // 目录项不为空，写回目录项的文件
                if (dentry_cursor->inode != NULL) {
                    nfs_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct nfs_dentry_d);

            }            
            i++;
        }
    }
    else if (NFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接把数据块内容写回block_index指向的磁盘块即可 */
        // 由于实验不要求文件的写，下面的循环实际不会执行
        for(int j = 0; j < inode->block_num; j++){
            if (nfs_driver_write(NFS_DATA_OFS(inode->block_index[j]), inode->block_pointer[j], 
                                NFS_BLKS_SZ(1)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return -NFS_ERROR_IO;
            }
        }
        
    }
    return NFS_ERROR_NONE;
}

/**
 * @brief 从磁盘中读取inode节点
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct sfs_inode* 
 */
struct nfs_inode* nfs_read_inode(struct nfs_dentry * dentry, int ino) {
    struct nfs_inode* inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    struct nfs_inode_d inode_d;
    struct nfs_dentry* sub_dentry;
    struct nfs_dentry_d dentry_d;
    int    dir_cnt = 0, offset;
    /* 从磁盘读索引结点 */
    if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        NFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    // 填写inode信息
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->block_num = inode_d.block_num;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    // 只读有效的block_index
    for(int j = 0; j < inode_d.block_num; j++){
        inode->block_index[j] = inode_d.block_index[j];
    }

    /* 内存中的inode的数据或子目录项部分也需要读出 */
    if (NFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        int k = 0;   // 循环变量，记录读取的是第几个block_index指向的数据块
        while(k < inode->block_num && dir_cnt > 0){
            offset = NFS_DATA_OFS(inode->block_index[k]);
            while (dir_cnt > 0 && (offset += sizeof(struct nfs_dentry_d) != NFS_DATA_OFS(inode->block_index[k] + 1)))
            {
                if (nfs_driver_read(offset, 
                                    (uint8_t *)&dentry_d, 
                                    sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                    NFS_DBG("[%s] io error\n", __func__);
                    return NULL;
                }
                sub_dentry = new_dentry(dentry_d.name, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                nfs_alloc_dentry(inode, sub_dentry);   // 将sub_dentry插入到inode中
                offset += sizeof(struct nfs_dentry_d);
                dir_cnt--;
            }
            k++;
        }
        
    }
    else if (NFS_IS_REG(inode)) {
        // 如果是文件类型直接读取数据即可
        // inode->data = (uint8_t *)malloc(SFS_BLKS_SZ(SFS_DATA_PER_FILE));
        // 空文件，实际不会执行下面的循环
        for(int j = 0; j < inode->block_num; j++){
            inode->block_pointer[j] = (uint8_t *)malloc(NFS_BLKS_SZ(1));
            if (nfs_driver_read(NFS_DATA_OFS(inode->block_index[j]), (uint8_t *)inode->block_pointer[j], 
                                NFS_BLKS_SZ(1)) != NFS_ERROR_NONE) {
                NFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
        }
    }
    return inode;
}

/**
 * @brief 获取inode的第dir个目录项
 * 
 * @param inode 索引节点
 * @param dir [0...]
 * @return struct nfs_dentry* 
 */
struct nfs_dentry* nfs_get_dentry(struct nfs_inode * inode, int dir) {
    struct nfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 查找文件或目录
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *  
 * 
 * 如果能查找到，返回该目录项
 * 如果查找不到，返回的是上一个有效的路径
 * 
 * path: /a/b/c
 *      1) find /'s inode     lvl = 1
 *      2) find a's dentry 
 *      3) find a's inode     lvl = 2
 *      4) find b's dentry    如果此时找不到了，is_find=FALSE且返回的是a的inode对应的dentry
 * 
 * @param path 
 * @return struct nfs_dentry* 
 */
struct nfs_dentry* nfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct nfs_dentry* dentry_cursor = nfs_super.root_dentry;   // 路径解析从根目录开始
    struct nfs_dentry* dentry_ret = NULL;   // 当前查找到的目录或文件
    struct nfs_inode*  inode; 
    int   total_lvl = nfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));   // 当前路径的复制
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 查找的是根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = nfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");   // 分隔路径，获取最外层（最左侧）目录名    
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            nfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        // 没遍历到目标层数就查询到普通文件，报错
        if (NFS_IS_REG(inode) && lvl < total_lvl) {
            NFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;   // 返回上一个有效路径
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;   // 查找成功，返回该目录项
                break;
            }
        }
        fname = strtok(NULL, "/");   // 继续获取下一层目录名
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
    }

    return dentry_ret;
}

/**
 * @brief 挂载nfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Inode | Data
 * 
 * BLK_SZ = 2 * IO_SZ
 * 
 * 8个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int nfs_mount(struct custom_options options){
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    struct nfs_super_d  nfs_super_d; 
    struct nfs_dentry*  root_dentry;
    struct nfs_inode*   root_inode;

    int                 inode_block_num;
    int                 map_inode_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    nfs_super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);   // 打开驱动

    if (driver_fd < 0) {
        return driver_fd;
    }

    // 向内存超级块中标记驱动并写入磁盘大小和单次IO大小
    nfs_super.fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &nfs_super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &nfs_super.sz_io);
    nfs_super.sz_blks = nfs_super.sz_io * 2;
    
    // 创建根目录项并读取磁盘超级块到内存
    root_dentry = new_dentry("/", NFS_DIR);     /* 根目录项每次挂载时新建 */

    if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d),    // 由于固定从NFS_SUPER_OFS中读取超级块信息，故nfs_super_d不需存储超级块位于磁盘中的逻辑块数和偏移
                        sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   
                                                      /* 读取super */
    if (nfs_super_d.magic_num != NFS_MAGIC_NUM) {     /* 幻数不正确，初始化 */
                                                      /* 估算各部分大小 */
        super_blks = NFS_SUPER_BLOCK_NUM;   // 超级块占用逻辑块数量

        inode_block_num  =  NFS_INODE_BLOCK_NUM;   // 索引节点占用逻辑块数量

        map_inode_blks = NFS_INODE_MAP_BLOCK_NUM;   // 索引节点位图占用逻辑块数量
                                                      /* 布局layout */
        // 先填充nfs_super_d，后赋值给nfs_super
        nfs_super_d.max_ino = inode_block_num * 8;    // inode数目(一个逻辑块放置8个索引节点)
        nfs_super_d.magic_num = NFS_MAGIC_NUM;   // 幻数
        nfs_super_d.max_data = NFS_DATA_BLOCK_NUM;   // 数据块数量 

        nfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);   // inode位图起始地址
        nfs_super_d.map_data_offset = nfs_super_d.map_inode_offset + NFS_BLKS_SZ(map_inode_blks);   // 数据块位图起始地址

        nfs_super_d.map_inode_blks  = map_inode_blks;   // inode位图所占数据块数量
        nfs_super_d.map_data_blks = NFS_DATA_MAP_BLOCK_NUM;   // 数据块位图所占逻辑块数量

        nfs_super_d.inode_offset = nfs_super_d.map_data_offset + NFS_BLKS_SZ(nfs_super_d.map_data_blks);   // 索引节点块起始地址
        nfs_super_d.data_offset = nfs_super_d.inode_offset + NFS_BLKS_SZ(inode_block_num);   // 数据块起始地址

        nfs_super_d.sz_usage    = 0;
        NFS_DBG("inode map blocks: %d\n", map_inode_blks);
        is_init = TRUE;
    }
    nfs_super.sz_usage   = nfs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    nfs_super.magic = nfs_super_d.magic_num;

    nfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
    nfs_super.map_inode_blks = nfs_super_d.map_inode_blks;
    nfs_super.map_inode_offset = nfs_super_d.map_inode_offset;
    nfs_super.max_ino = nfs_super_d.max_ino;

    nfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));
    nfs_super.map_data_blks = nfs_super_d.map_data_blks;
    nfs_super.map_data_offset = nfs_super_d.map_data_offset;
    nfs_super.max_data = nfs_super_d.max_data;

    nfs_super.inode_offset = nfs_super_d.inode_offset;
    nfs_super.data_offset = nfs_super_d.data_offset;

    // nfs_dump_map();

	printf("\n--------------------------------------------------------------------------------\n\n");

    // 初始化inode位图
    if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                        NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 初始化数据块位图
    if (nfs_driver_read(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), 
                        NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = nfs_alloc_inode(root_dentry);
        nfs_sync_inode(root_inode);
    }
    
    // 初始化根目录项
    root_inode            = nfs_read_inode(root_dentry, NFS_ROOT_INO);  /* 读取根目录 */
    root_dentry->inode    = root_inode;
    nfs_super.root_dentry = root_dentry;
    nfs_super.is_mounted  = TRUE;

    // nfs_dump_map();

    return ret;
}

/**
 * @brief 卸载文件系统
 * 
 * @return int 
 */
int nfs_umount() {
    struct nfs_super_d  nfs_super_d; 

    if (!nfs_super.is_mounted) {
        return NFS_ERROR_NONE;
    }

    nfs_sync_inode(nfs_super.root_dentry->inode);     /* 从根节点向下刷写节点，将其刷回磁盘 */

    // 利用nfs_super字段填写nfs_super_d相关字段，并将nfs_super_d写入磁盘                                              
    nfs_super_d.magic_num           = NFS_MAGIC_NUM;

    nfs_super_d.max_ino             = nfs_super.max_ino;
    nfs_super_d.max_data            = nfs_super.max_data;

    nfs_super_d.map_inode_blks      = nfs_super.map_inode_blks;
    nfs_super_d.map_data_blks       = nfs_super.map_data_blks;

    nfs_super_d.map_inode_offset    = nfs_super.map_inode_offset;
    nfs_super_d.map_data_offset     = nfs_super.map_data_offset;

    nfs_super_d.inode_offset        = nfs_super.inode_offset;
    nfs_super_d.data_offset         = nfs_super.data_offset;

    nfs_super_d.sz_usage            = nfs_super.sz_usage;
    
    if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d, 
                     sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 将inode位图写入磁盘
    if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                         NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    // 将数据块位图写入磁盘
    if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), 
                         NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    free(nfs_super.map_inode);   // 释放inode位图
    free(nfs_super.map_data);   // 释放数据块位图

    ddriver_close(NFS_DRIVER());   // 关闭驱动

    return NFS_ERROR_NONE;
}