
```sh
#gdisk /dev/vda
Command (? for help): p
Disk /dev/vda: 1065353216 sectors, 508.0 GiB
Logical sector size: 512 bytes
Disk identifier (GUID): CD030040-0453-40AD-802F-0A79457C8764
Partition table holds up to 128 entries
First usable sector is 34, last usable sector is 1065353182
Partitions will be aligned on 2048-sector boundaries
Total free space is 1048577981 sectors (500.0 GiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   1            2048        16777215   8.0 GiB     8300  Linux filesystem

# growpart /dev/vda 1
CHANGED: partition=1 start=2048 old: size=16775168 end=16777216 new: size=1065348427,end=1065350475

#gdisk /dev/vda
Command (? for help): p
Disk /dev/vda: 1065353216 sectors, 508.0 GiB
Logical sector size: 512 bytes
Disk identifier (GUID): DF992A06-F627-446F-B57D-EB92502FC9CF
Partition table holds up to 128 entries
First usable sector is 34, last usable sector is 1065353182
Partitions will be aligned on 2048-sector boundaries
Total free space is 4722 sectors (2.3 MiB)

Number  Start (sector)    End (sector)  Size       Code  Name
   1            2048      1065350474   508.0 GiB   8300  Linux filesystem



#mount -t xfs -o nouuid /dev/vda1 /mnt

#df -lh
/dev/vda1       8.0G  2.4G  5.6G  30% /mnt

# xfs_growfs /dev/vda1 
# df -lh
/dev/vda1       508G  2.5G  506G   1% /mnt
```