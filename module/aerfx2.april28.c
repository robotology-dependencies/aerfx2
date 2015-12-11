///////////////////////////////////////////////////////////
// aerfx2.c
//

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <asm/string.h>
#include <asm/byteorder.h>
#include <linux/ioctl.h>

///////////////////////////////////////////////////////////
// for pre 2.6.35, activate this section by changing 0 to 1
// version of 28th April 

#if 1
#define usb_alloc_coherent usb_buffer_alloc
#define usb_free_coherent usb_buffer_free
#endif

// all this does not work as wished..:
//#if VERSION = 2 && PATCHLEVEL = 6 && SUBLEVEL < 35
//#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
///////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////
// macros
//


///////////////////////////////////////////////////////////
// defines

// uncomment this to enable zero-URB debugging
#define DEBUG_PRINT_ZEROLEN_URB

// uncomment the following line to use this driver with the USBAERmini2 board! 
//#define AERFX2_USBAERMINI2_SUPPORT

// do local loopback for test purpose
//#define LOOPBACK_TEST


///////////////////////
// board type stuff

#define BOARDTYPE_UNKNOWN 0

#define BOARDTYPE_AEX 1
#define USB_AEX_VENDOR_ID  0x04b4
#define USB_AEX_PRODUCT_ID 0x8613
#define USB_AEX_ALTSETTING 1

#define BOARDTYPE_USBAERMINI2 2
#define USB_USBAERMINI2_VENDOR_ID  0x0547
#define USB_USBAERMINI2_PRODUCT_ID 0x8801


#define AERFX2_OUT_EP 0x02
#define AERFX2_IN_EP  0x06
///////////////////////


// FIXME: workaround for collision with cfw002
#define USB_SKEL_MINOR_BASE       192
#define AERFX2_MINOR_BASE         USB_SKEL_MINOR_BASE


#define AERIN_ALLOC_LIMIT         (32*512)
#define AERIN_URBSIZE_MAX         AERIN_ALLOC_LIMIT

// AERIN_URBSIZE_MAX rounded down to 512B multiple:
#define AERFX2_INURB_SIZE         ((size_t) ((AERIN_URBSIZE_MAX / 512) * 512))
#define AERFX2_INURB_EVENTS_MAX   (AERFX2_INURB_SIZE / sizeof(struct aerfx2_data))

#define AERFX2_OUTURB_SIZE        AERFX2_INURB_SIZE
#define AERFX2_OUTURB_EVENTS_MAX  AERFX2_INURB_EVENTS_MAX

#define AERFX2_INURB_COUNT        64
#define AERFX2_OUTURB_COUNT       64


///////////////////////////////////////////////////////////
// types

#ifndef u8
#define u8 uint8_t
#endif

#ifndef u32
#define u32 uint32_t
#endif

#ifndef i32
#define i32 int32_t
#endif

#define FLAGS_TYPE unsigned long

// data used in AEX
// be careful about endianness!
struct aerfx2_data {
    u32 timestamp;
    u32 address;
};

// per device data for our driver
struct usb_aerfx2 {
    struct usb_device *     udev;           /* the usb device for this device */
    struct usb_interface *  interface;      /* the interface for this device */
    struct kref             kref;

    int                     in_ep;
    int                     out_ep;

    struct urb *            inurbs[AERFX2_INURB_COUNT];
    struct urb *            outurbs[AERFX2_OUTURB_COUNT];

    spinlock_t              ilock;
    spinlock_t              olock;

    struct tasklet_struct   tasklet;

    struct list_head        completed_inurbs; // protected by .ilock
    struct list_head        idle_outurbs;     // protected by .olock

    int                     opencount;        // protected by .ilock

    int                     boardtype;
};
#define to_aerfx2_dev(d) container_of(d, struct usb_aerfx2, kref)


///////////////////////////////////////////////////////////
// function prototypes

static void aerfx2_bulk_in_callback(struct urb *urb);
static void aerfx2_bulk_in_tasklet(unsigned long data);
static void aerfx2_bulk_out_callback(struct urb *urb);

static int aerfx2_open(struct inode *inode, struct file *file);
static int aerfx2_release(struct inode *inode, struct file *file);
static ssize_t aerfx2_read(struct file *file, char *buffer, size_t count, loff_t *ppos);
static ssize_t aerfx2_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos);

static void aerfx2_delete(struct kref *kref);

static int aerfx2_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void aerfx2_disconnect(struct usb_interface *interface);


///////////////////////////////////////////////////////////
// static variables

// table of devices that work with this driver 
static struct usb_device_id aerfx2_table [] = {
    { USB_DEVICE(USB_AEX_VENDOR_ID, USB_AEX_PRODUCT_ID) },
#ifdef AERFX2_USBAERMINI2_SUPPORT
    { USB_DEVICE(USB_USBAERMINI2_VENDOR_ID, USB_USBAERMINI2_PRODUCT_ID) },
#endif
    { }                 // Terminating entry 
};
MODULE_DEVICE_TABLE (usb, aerfx2_table);


// usb driver description
static struct usb_driver aerfx2_driver = {
    .name =         "aerfx2",
    .probe =        aerfx2_probe,
    .disconnect =   aerfx2_disconnect,
    .id_table =     aerfx2_table,
};

// device descriptor struct (fops)
static struct file_operations aerfx2_fops = {
    .owner =    THIS_MODULE,
    .read =     aerfx2_read,
    .write =    aerfx2_write,
    .open =     aerfx2_open,
    .release =  aerfx2_release,
};

// device descriptor struct
static struct usb_class_driver aerfx2_device = {
    .name =         "aerfx2_%d",
    .fops =         &aerfx2_fops,
    .minor_base =   AERFX2_MINOR_BASE,
};


///////////////////////////////////////////////////////////
// functions


///////////////////////////////////////////////////////////
// AEX control (EP1OUT bulk messages)

#define EP1CMD_MONITOR (0x01)

void aex_monitor(struct usb_aerfx2 *dev, int enable) {
    int ret, rlen;
    const int urblen = 64;
    unsigned char urbdata[urblen];

    if (dev->boardtype != BOARDTYPE_AEX) return;

    if (enable) {
        dev_info(&dev->udev->dev, "aex_monitor: enable\n");
    } else {
        dev_info(&dev->udev->dev, "aex_monitor: disable\n");
    }

    urbdata[0] = EP1CMD_MONITOR;
    if (enable) {
        urbdata[1] = 0x01;
    } else {
        urbdata[1] = 0x00;
    } 
   

    ret = usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, 1), urbdata, urblen, &rlen, 1000);

    if (ret) {
        dev_dbg(&dev->udev->dev, "%s, usb_bulk_msg: %d\n", __FUNCTION__, ret);
    }
}


///////////////////////////////////////////////////////////
// IN URB handling

// ''top half'' / IRQ
static void aerfx2_bulk_in_callback(struct urb *urb)
{
    struct usb_aerfx2 * dev;
    FLAGS_TYPE flags;
    dev = (struct usb_aerfx2*)urb->context;

    spin_lock_irqsave(&dev->ilock, flags);
        list_add_tail(&urb->urb_list, &dev->completed_inurbs);
        dev->tasklet.data = (unsigned long)dev;
        tasklet_schedule(&dev->tasklet);
    spin_unlock_irqrestore(&dev->ilock, flags);
}

// ''bottom half'' / tasklet
static void aerfx2_bulk_in_tasklet(unsigned long data)
{
    struct usb_aerfx2 * dev;
    FLAGS_TYPE flags;
    struct list_head myurbs;
    struct urb * urb;
    void *buf;
    int retval;
    int mycount;

    INIT_LIST_HEAD(&myurbs);
    dev = (struct usb_aerfx2*)data;
  
    // do nothing if someone is monitoring !!!
    if (dev->opencount > 0) return;


    // clear and resubmit all inurbs if opencount <= 0

    // first get all urbs while locked
    //////////////////////////
    spin_lock_irqsave(&dev->ilock, flags);
    mycount = 0;
    while (1) {
        // urb available?
        if ( list_empty(&dev->completed_inurbs) ) {
            break;
        }

        // get the first urb 
        urb = list_entry(dev->completed_inurbs.next, struct urb, urb_list);
        // remove it from the list 
        list_del(&urb->urb_list);        
        // add to myurbs
        list_add_tail(&urb->urb_list, &myurbs);

        mycount++;
    }
    spin_unlock_irqrestore(&dev->ilock, flags);
    //////////////////////////
    

    // resubmit them...
    while (1) {
        // urb available?
        if ( list_empty(&myurbs) ) {
            break;
        }

        // get the first urb 
        urb = list_entry(myurbs.next, struct urb, urb_list);
        // remove it from the list 
        list_del(&urb->urb_list);        

        buf = urb->transfer_buffer;
    
        usb_fill_bulk_urb(urb, dev->udev,
              usb_rcvbulkpipe(dev->udev, dev->in_ep),
              buf, AERFX2_INURB_SIZE, aerfx2_bulk_in_callback, (void*)dev);
        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
#ifdef URB_ASYNC_UNLINK
        urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif

#ifndef LOOPBACK_TEST
        // submit
        retval = usb_submit_urb(urb, GFP_ATOMIC);
        switch (retval) {
        case 0: // success
            break;
        case -ENODEV: // device disconnected
            break;
        default:
            dev_err(&dev->udev->dev, "%s - failed submitting IN URB, error %d, URB: %p\n", __FUNCTION__, retval, urb);
        }
#else
        spin_lock_irqsave(&dev->olock, flags);
            list_add(&urb->urb_list, &dev->idle_outurbs);
        spin_unlock_irqrestore(&dev->olock, flags);
#endif
        continue;
    }
}

///////////////////////////////////////////////////////////
// OUT URB handling

static void aerfx2_bulk_out_callback(struct urb *urb)
{
    // check urb status and put the urb to the idle_outurbs list...
    struct usb_aerfx2 * dev;
    FLAGS_TYPE flags;

    dev = (struct usb_aerfx2*)urb->context;

    switch (urb->status) {
    case 0:
        goto reuse_urb;
    case -ENOENT:       // unlinked
    case -ECONNRESET:   // unlinked
    case -ESHUTDOWN:    // persistent HW problem
        goto dump_urb;
    case -EPROTO:
        dev_err(&dev->udev->dev, "%s got -EPROTO!\n", __FUNCTION__);
        goto dump_urb;
    default:
        dev_info(&dev->udev->dev, "%s - nonzero status: %d\n", __FUNCTION__, urb->status);
        goto reuse_urb;
    }

reuse_urb:
    spin_lock_irqsave(&dev->olock, flags);
        list_add(&urb->urb_list, &dev->idle_outurbs);
    spin_unlock_irqrestore(&dev->olock, flags);
    return;

dump_urb:
    dev_err(&dev->udev->dev, "dumping OUT URB, status %d, URB: %p\n", urb->status, urb);
    return;
}


///////////////////////////////////////////////////////////
// File ops

static int aerfx2_open(struct inode *inode, struct file *file)
{
    struct usb_aerfx2 *dev;
    struct usb_interface *interface;
    int subminor;
    int retval = 0;
    int oc;
    FLAGS_TYPE flags;

    subminor = iminor(inode);

    interface = usb_find_interface(&aerfx2_driver, subminor);
    if (!interface) {
        pr_err("%s - error, can't find device for minor %d\n", __FUNCTION__, subminor);
        retval = -ENODEV;
        goto exit;
    }

    dev = usb_get_intfdata(interface);
    if (!dev) {
        retval = -ENODEV;
        goto exit;
    }

    /* check for O_NONBLOCK */
    if ((file->f_flags & O_NONBLOCK) == 0) {
        dev_err(&dev->udev->dev, "%s - error, open() called without O_NONBLOCK, blocking IO not supported!\n", __FUNCTION__);
        retval = -EINVAL;
        goto exit;
    }

    /* increment our usage count for the device */
    kref_get(&dev->kref);

    /* save our object in the file's private structure */
    file->private_data = dev;

    // increment open count
    spin_lock_irqsave(&dev->ilock, flags);
        dev->opencount++;
        oc = dev->opencount;
    spin_unlock_irqrestore(&dev->ilock, flags);
    dev_info(&dev->udev->dev, "opencount: %d\n", oc);

    if (oc == 1) {
        aex_monitor(dev, 1);
    }

exit:
    return retval;
}

static int aerfx2_release(struct inode *inode, struct file *file)
{
    struct usb_aerfx2 *dev;
    FLAGS_TYPE flags;
    int oc;

    dev = (struct usb_aerfx2 *)file->private_data;
    if (dev == NULL)
        return -ENODEV;

    // decrement open count
    spin_lock_irqsave(&dev->ilock, flags);
        dev->opencount--;
        oc = dev->opencount;
    spin_unlock_irqrestore(&dev->ilock, flags);

    dev_info(&dev->udev->dev, "opencount: %d\n", oc);

    if (oc == 0) {
        aex_monitor(dev, 0);
    }

    // resubmit all the completed urbs if opencount == zero !
    // to do so we schedule the IN URB tasklet...
    if (oc == 0) {
        spin_lock_irqsave(&dev->ilock, flags);
        dev->tasklet.data = (unsigned long)dev;
        tasklet_schedule(&dev->tasklet);
        spin_unlock_irqrestore(&dev->ilock, flags);
    }

    /* decrement the count on our device */
    kref_put(&dev->kref, aerfx2_delete);
    return 0;
}

static ssize_t aerfx2_read(struct file *file, char *user_buffer, size_t count, loff_t *ppos)
{
    size_t space_left;
    size_t size = 0;
    size_t copied;
    unsigned long failcount;
    size_t ret;
    FLAGS_TYPE flags;
    void *buf;
#ifndef LOOPBACK_TEST
    int retval;
#endif
    struct urb * urb;
    struct list_head myurbs;
    struct usb_aerfx2 *dev;
    int ok;

    INIT_LIST_HEAD(&myurbs);
    dev = (struct usb_aerfx2 *)file->private_data;


    //////////////////////////
    spin_lock_irqsave(&dev->ilock, flags);

    if ( list_empty(&dev->completed_inurbs) ) {
        spin_unlock_irqrestore(&dev->ilock, flags);
        ret = -EAGAIN;
        goto exit;
    }


    ok = 0;
    space_left = count;
    while (1) {
        // urb available?
        if ( list_empty(&dev->completed_inurbs) ) {
            break;
        }

        // get the first urb 
        urb = list_entry(dev->completed_inurbs.next, struct urb, urb_list);

        buf = urb->transfer_buffer;
        size = urb->actual_length;

        // check for reasonalbe URB size
        if (size % 8) {
            dev_err(&dev->udev->dev, "crappy URB size: %zu %% 8 == %zu!\n", size, size % 8);
        }

        if (urb->status) {
            // urb has some error status. do not count data
            size = 0;
        }

        if (space_left < size) {
            // no more space in read() buf for next this urb...
            break;
        }

        // remove it from the list 
        list_del(&urb->urb_list);        
        // add to myurbs
        list_add_tail(&urb->urb_list, &myurbs);

        space_left -= size;
        ok = 1;
        if (size == 0) break;
    }
    spin_unlock_irqrestore(&dev->ilock, flags);
    //////////////////////////


    // could we fit at least some data?
    //if (space_left == count) {
    if (!ok) {
        dev_err(&dev->udev->dev, "your read was too small! you: %zu, urb: %zu\n", count, size);
        dev_err(&dev->udev->dev, "do not read less than AERFX2_INURB_SIZE = %zu!\n", AERFX2_INURB_SIZE);
        ret = -EINVAL;
        goto exit;
    }


    copied = 0;
    while (1) {
        // urb available?
        if ( list_empty(&myurbs) ) {
            break;
        }

        // get the first urb 
        urb = list_entry(myurbs.next, struct urb, urb_list);
        // remove it from the list 
        list_del(&urb->urb_list);        

        buf = urb->transfer_buffer;
        size = urb->actual_length;

        switch (urb->status) {
        case 0:
            goto urb_ok;
        case -ENOENT:       // unlinked
        case -ECONNRESET:   // unlinked
        case -ESHUTDOWN:    // persistent HW problem
            goto dump_urb;
        case -EPROTO:
            dev_err(&dev->udev->dev, "%s got -EPROTO!\n", __FUNCTION__);
            goto dump_urb;
        default:
            dev_err(&dev->udev->dev, "%s - nonzero status: %d\n", __FUNCTION__, urb->status);
            goto resubmit_urb;
        }

    urb_ok:
        /////////////////////////////////////////
        // transfer data to userspace

        if (size) {

            failcount = copy_to_user(user_buffer, buf, size);
            if ( failcount ) {
                dev_warn(&dev->udev->dev, "copy_to_user %lu/%zu failed!\n", failcount, size);
                dev_warn(&dev->udev->dev, "read would have been %zu out of %zu.\n", count - space_left, count);
                ret = -EFAULT;
                goto exit;
            }
            user_buffer += size;
            copied += size;
        } else {
            // got a zero length URB
#ifdef DEBUG_PRINT_ZEROLEN_URB
            if (printk_ratelimit()) printk(KERN_INFO "got a zero length URB...\n");
#endif
        }

        // fall through to resubmit

    resubmit_urb:
        /////////////////////////////////////////
        // reuse the URB
     
        usb_fill_bulk_urb(urb, dev->udev,
              usb_rcvbulkpipe(dev->udev, dev->in_ep),
              buf, AERFX2_INURB_SIZE, aerfx2_bulk_in_callback, (void*)dev);
        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
#ifdef URB_ASYNC_UNLINK
        urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif

#ifndef LOOPBACK_TEST
        // submit
        retval = usb_submit_urb(urb, GFP_ATOMIC);
        if (retval) {
            dev_err(&dev->udev->dev, "%s - failed submitting IN URB, error %d, URB: %p\n", __FUNCTION__, retval, urb);
        }
#else
        spin_lock_irqsave(&dev->olock, flags);
            list_add(&urb->urb_list, &dev->idle_outurbs);
        spin_unlock_irqrestore(&dev->olock, flags);
#endif
        continue;

    dump_urb:
        dev_err(&dev->udev->dev, "dumping IN URB, status %d, URB: %p\n", urb->status, urb);
        continue;
    }


    if (copied > count || copied != count - space_left) {
        dev_crit(&dev->udev->dev, "FATAL PROGRAMMING ERROR! %s:%d --- %zu %zu %zu\n", __FUNCTION__, __LINE__, copied, count, space_left);
        ret = -EPROTO;
        goto exit;
    }

    ret = copied;

exit:
    return ret;
}

static ssize_t aerfx2_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos)
{
    size_t write_left;
    size_t size = 0;
    size_t copied;
    unsigned long failcount;
    size_t ret;
    FLAGS_TYPE flags;
    void *buf;
#ifndef LOOPBACK_TEST
    int retval;
#endif
    struct urb * urb;
    struct list_head myurbs;
    struct usb_aerfx2 *dev;

    INIT_LIST_HEAD(&myurbs);
    dev = (struct usb_aerfx2 *)file->private_data;



    //////////////////////////
    spin_lock_irqsave(&dev->olock, flags);

    if ( list_empty(&dev->idle_outurbs) ) {
        spin_unlock_irqrestore(&dev->olock, flags);
        ret = -EAGAIN;
        goto exit;
    }


    write_left = count;
    while (write_left > 0) {
        // urb available?
        if ( list_empty(&dev->idle_outurbs) ) {
            break;
        }

        // get the first urb 
        urb = list_entry(dev->idle_outurbs.next, struct urb, urb_list);
        // remove it from the list 
        list_del(&urb->urb_list);        
        // add to myurbs
        list_add_tail(&urb->urb_list, &myurbs);

        buf = urb->transfer_buffer;
        size = AERFX2_OUTURB_SIZE;

        if (write_left < size) size = write_left;

        write_left -= size;
    }
    spin_unlock_irqrestore(&dev->olock, flags);
    //////////////////////////



    copied = 0;
    while (1) {
        // urb available?
        if ( list_empty(&myurbs) ) {
            break;
        }

        // get the first urb 
        urb = list_entry(myurbs.next, struct urb, urb_list);
        // remove it from the list 
        list_del(&urb->urb_list);        

        buf = urb->transfer_buffer;

        size = count - copied;
        if (size > AERFX2_OUTURB_SIZE) {
            size = AERFX2_OUTURB_SIZE;
        }

        /////////////////////////////////////////
        // transfer data from userspace

        failcount = copy_from_user(buf, user_buffer, size);
        if ( failcount ) {
            dev_warn(&dev->udev->dev, "copy_from_user %lu/%zu failed!\n", failcount, size);
            dev_warn(&dev->udev->dev, "write would have been %zu out of %zu.\n", count - write_left, count);
            ret = -EFAULT;
            goto exit;
        }
        user_buffer += size;
        copied += size;

        usb_fill_bulk_urb(urb, dev->udev,
              usb_sndbulkpipe(dev->udev, dev->out_ep),
              buf, size, aerfx2_bulk_out_callback, (void*)dev);
        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
#ifdef URB_ASYNC_UNLINK
        urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif

#ifndef LOOPBACK_TEST
        // submit
        retval = usb_submit_urb(urb, GFP_ATOMIC);
        if (retval) {
            dev_err(&dev->udev->dev, "%s - failed submitting OUT URB, error %d, URB: %p\n", __FUNCTION__, retval, urb);
        }
#else
        // loopback by directly calling IN IRQ callback
        urb->status = 0; // fake status...
        urb->actual_length = urb->transfer_buffer_length; // fake length
        aerfx2_bulk_in_callback(urb);
#endif
    }


    if (copied > count || copied != count - write_left) {
        dev_err(&dev->udev->dev, "FATAL PROGRAMMING ERROR! %s:%d --- %zu %zu %zu\n", __FUNCTION__, __LINE__, copied, count, write_left);
        ret = -EPROTO;
        goto exit;
    }

    ret = copied;

exit:
    return ret;
}


//////////////////////////////////////////////////////////////////////////
// aerfx2_delete
// this is called if kref decrements to zero.

static void aerfx2_delete(struct kref *kref)
{   
    struct usb_aerfx2 *dev = to_aerfx2_dev(kref);
    struct urb * u;
    int i;
    FLAGS_TYPE flags;

    dev_info(&dev->udev->dev, "%s\n", __FUNCTION__);


    // try avoiding "WARNING: at /build/buildd/linux-2.6.24/arch/x86/kernel/pci-dma_32.c:66 dma_free_coherent()"
    // by  calling usb_free_buffer after spinlock released...

    // clean up in urbs
    for (i = 0; i < AERFX2_INURB_COUNT; i++) {
        struct usb_device * t_dev;
        void *t_buffer;
        dma_addr_t t_dma;

        u = dev->inurbs[i];
        if (!u) continue;

        t_dev = u->dev;
        t_buffer = u->transfer_buffer;
        t_dma = u->transfer_dma;

        spin_lock_irqsave(&dev->ilock, flags);        
            usb_unlink_urb(u);
            usb_free_urb(u);
        spin_unlock_irqrestore(&dev->ilock, flags);

        usb_free_coherent(t_dev, AERFX2_INURB_SIZE, t_buffer, t_dma);
    }

    // clean up out urbs
    for (i = 0; i < AERFX2_OUTURB_COUNT; i++) {
        struct usb_device * t_dev;
        void *t_buffer;
        dma_addr_t t_dma;

        u = dev->outurbs[i];
        if (!u) continue;

        t_dev = u->dev;
        t_buffer = u->transfer_buffer;
        t_dma = u->transfer_dma;

        spin_lock_irqsave(&dev->olock, flags);        
            usb_unlink_urb(u);
            usb_free_urb(u);
        spin_unlock_irqrestore(&dev->olock, flags);        

        usb_free_coherent(t_dev, AERFX2_OUTURB_SIZE, t_buffer, t_dma);
    }

    
    pr_info("aerfx2: pre dev_info() call, &dev->udev->dev:%p\n", &dev->udev->dev);
    dev_info(&dev->udev->dev, "usb_put_dev\n");

    pr_info("aerfx2: pre usb_put_dev() call, dev->udev:%p\n", dev->udev);
    usb_put_dev(dev->udev);

    pr_info("aerfx2: pre kfree() call, dev:%p\n", dev);
    kfree (dev);

    pr_info("aerfx2: kfree completed, returning from aerfx2_delete()\n");
}

//////////////////////////////////////////////////////////////////////////
// aerfx2_probe + aerfx2_disconnect
// 
// called by usb stack on device connect / disconnect.
//

static int aerfx2_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_aerfx2 *dev = NULL;
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;
    int retval = -ENOMEM;
    struct urb * urb;
    char* buf;
    int ret;

    __u16 idVendor, idProduct;

    pr_info("%s\n", __FUNCTION__);

    idVendor = id->idVendor;
    idProduct = id->idProduct;

    // allocate memory for our device state and initialize it
    dev = kmalloc(sizeof(*dev), GFP_ATOMIC);
    if (dev == NULL) {
        pr_err("Out of memory!\n");
        goto error;
    }
    memset(dev, 0, sizeof(*dev));
    
    kref_init(&dev->kref); // init kref (sets it to 1)

    spin_lock_init(&dev->ilock);
    spin_lock_init(&dev->olock);

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    dev->opencount = 0;

    // check speed, force highspeed only for all boards!
    if (dev->udev->speed != USB_SPEED_HIGH) {
        dev_info(&dev->udev->dev, "device not in highspeed mode! try programming firmware...\n");
        return -EAGAIN;
    }

    // what board do we have? 
    dev->boardtype = BOARDTYPE_UNKNOWN;
    if (idVendor == USB_AEX_VENDOR_ID && idProduct == USB_AEX_PRODUCT_ID) {
        dev->boardtype = BOARDTYPE_AEX;
    }
    if (idVendor == USB_USBAERMINI2_VENDOR_ID && idProduct == USB_USBAERMINI2_PRODUCT_ID) {
        dev->boardtype = BOARDTYPE_USBAERMINI2;
    }
    dev_info(&dev->udev->dev, "fount board type: %d\n", dev->boardtype);
    
    
    // only for blank FX2! i.e. AEX without FX2 flash programmed...
    if (dev->boardtype == BOARDTYPE_AEX) {
        usb_set_interface(dev->udev, dev->interface->altsetting[0].desc.bInterfaceNumber, USB_AEX_ALTSETTING);
        aex_monitor(dev, 0);
    }

    // USBAERmini2 setup
    if (dev->boardtype == BOARDTYPE_USBAERMINI2) {
        dev_info(&dev->udev->dev, "try to set up usbaermini2...\n");

        // 0xB3 - EnableAE IN
        ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
            0xB3, 0x00, 0x00, 0x00, NULL, 0, 1000);

        dev_info(&dev->udev->dev, "ret: %d\n", ret);

        // 0xC6 - Enable AE
        ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
            0xC6, 0x00, 0x00, 0x00, NULL, 0, 1000);

        dev_info(&dev->udev->dev, "ret: %d\n", ret);
    }

    // read the endpoint information
    iface_desc = interface->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        dev_info(&dev->udev->dev, "found endpoint: 0x%02x, psize: %d\n", endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize));
    }

    // this is the same for all boards so far:
    dev->out_ep = AERFX2_OUT_EP;
    dev->in_ep = AERFX2_IN_EP;

    // save our data pointer in this interface device
    usb_set_intfdata(interface, dev);

    // we can register the device now, as it is ready
    retval = usb_register_dev(interface, &aerfx2_device);
    if (retval) {
        dev_err(&dev->udev->dev, "Not able to get a minor for this device.\n");

        usb_set_intfdata(interface, NULL);
        goto error;
    }

    // listhead init 
    INIT_LIST_HEAD(&dev->completed_inurbs);
    INIT_LIST_HEAD(&dev->idle_outurbs);

    // tasklet init
    tasklet_init(&dev->tasklet, aerfx2_bulk_in_tasklet, (unsigned long)dev);

    ///////////////////////////////////////////////////////////
    // now we create and submit some IN URBs to read data...
    dev_info(&dev->udev->dev, "%s creating %d IN URBs of size %zu B.\n", __FUNCTION__, AERFX2_INURB_COUNT, AERFX2_INURB_SIZE);

    for (i = 0; i < AERFX2_INURB_COUNT; i++) {
        // allocate URB
        urb = usb_alloc_urb(0, GFP_ATOMIC);
        if (!urb) {
            dev_err(&dev->udev->dev, "%s IN usb_alloc_urb failed!\n", __FUNCTION__);
            retval = -ENOMEM;
            goto error;
        }

        // allocate its buffer
        buf = usb_alloc_coherent(dev->udev, AERFX2_INURB_SIZE, GFP_ATOMIC, &urb->transfer_dma);
        if (!buf) {
            dev_err(&dev->udev->dev, "%s IN usb_alloc_coherent failed!\n", __FUNCTION__);
            retval = -ENOMEM;
            goto error;
        }
                
        // fill URB
        usb_fill_bulk_urb(urb, dev->udev,
              usb_rcvbulkpipe(dev->udev, dev->in_ep),
              buf, AERFX2_INURB_SIZE, aerfx2_bulk_in_callback, (void*)dev);
        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
#ifdef URB_ASYNC_UNLINK
        urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif

        // submit
        retval = usb_submit_urb(urb, GFP_ATOMIC);
        if (retval) {
            dev_err(&dev->udev->dev, "%s - failed submitting read urb, error %d\n", __FUNCTION__, retval);
            goto error;
        }
        // put in our list
        dev->inurbs[i] = urb;
    }   
    ///////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////
    // now we create some OUT URBs to send data...
    dev_info(&dev->udev->dev, "%s creating %d OUT URBs of size %zu B.\n", __FUNCTION__, AERFX2_OUTURB_COUNT, AERFX2_OUTURB_SIZE);

    for (i = 0; i < AERFX2_OUTURB_COUNT; i++) {
        // allocate URB
        urb = usb_alloc_urb(0, GFP_ATOMIC);
        if (!urb) {
            dev_err(&dev->udev->dev, "%s OUT usb_alloc_urb failed!\n", __FUNCTION__);
            retval = -ENOMEM;
            goto error;
        }

        // allocate its buffer
        buf = usb_alloc_coherent(dev->udev, AERFX2_OUTURB_SIZE, GFP_ATOMIC, &urb->transfer_dma);
        if (!buf) {
            dev_err(&dev->udev->dev, "%s OUT usb_alloc_coherent failed!\n", __FUNCTION__);
            retval = -ENOMEM;
            goto error;
        }
                
        // fill URB structure
        usb_fill_bulk_urb(urb, dev->udev, usb_sndbulkpipe(dev->udev, dev->out_ep), buf, 0, NULL, (void*)dev);
        urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
#ifdef URB_ASYNC_UNLINK
        urb->transfer_flags |= URB_ASYNC_UNLINK;
#endif

        // put in our list
        dev->outurbs[i] = urb;

        // add to idle urbs
        list_add(&urb->urb_list, &dev->idle_outurbs);
    }   
    ///////////////////////////////////////////////////////////

    return 0;

error:
    if (dev) kref_put(&dev->kref, aerfx2_delete);
    return retval;
}

static void aerfx2_disconnect(struct usb_interface *interface)
{
    struct usb_aerfx2 *dev;
    int minor = interface->minor;

    // prevent aerfx2_open() from racing aerfx2_disconnect()
    // FIXME replace BKL by something sane
    // FIXME lock_kernel();

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    // deregister device node
    usb_deregister_dev(interface, &aerfx2_device);

    // FIXME replace BKL by something sane
    // FIXME unlock_kernel();
    
    // tasklet cleanup
    tasklet_kill(&dev->tasklet);

    // decrement our usage count
    kref_put(&dev->kref, aerfx2_delete);

    pr_info("aerfx2 minor %d now disconnected\n", minor);
}

///////////////////////////////////////////////////////////
// module init & exit

static int __init aerfx2_init(void)
{
    int result = 0;

    pr_info("%s\n", __FUNCTION__);

    // some sanity checks
    pr_info("sizeof(aerfx2_data) == %zu\n", sizeof(struct aerfx2_data));
    pr_info("INURB size: %zu\n", AERFX2_INURB_SIZE);

    if (AERFX2_INURB_SIZE % 512) {
        pr_err("invalud INURB size!\n");
        result = -EINVAL;
        goto ret;
    }

    // register
    pr_info("registering aerfx2 usb driver\n");
    result = usb_register(&aerfx2_driver);
    if (result) {
        pr_err("usb_register aerfx2 failed. Error number %d\n", result);
        goto ret;
    }

    goto ret;

ret:
    pr_info("%s returns: %d\n", __FUNCTION__, result);
    return result;
}

static void __exit aerfx2_exit(void)
{
    pr_info("%s\n", __FUNCTION__);
    usb_deregister(&aerfx2_driver);
}

///////////////////////////////////////////////////////////

module_init(aerfx2_init);
module_exit(aerfx2_exit);

MODULE_LICENSE("GPL");
///////////////////////////////////////////////////////////

