#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "amcc_s5933.h"
#include "8253.h"
#include "comedi_pci.h"
#include "comedi_fc.h"

#define PCI_VENDOR_ID_AMCC	0x10e8

#undef PCI9118_PARANOIDCHECK	

#undef PCI9118_EXTDEBUG		

#undef DPRINTK
#ifdef PCI9118_EXTDEBUG
#define DPRINTK(fmt, args...) printk(fmt, ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define IORANGE_9118 	64	
#define PCI9118_CHANLEN	255	

#define PCI9118_CNT0	0x00	
#define PCI9118_CNT1	0x04	
#define PCI9118_CNT2	0x08	
#define PCI9118_CNTCTRL	0x0c	
#define PCI9118_AD_DATA	0x10	
#define PCI9118_DA1	0x10	
#define PCI9118_DA2	0x14
#define PCI9118_ADSTAT	0x18	
#define PCI9118_ADCNTRL	0x18	
#define PCI9118_DI	0x1c	
#define PCI9118_DO	0x1c	
#define PCI9118_SOFTTRG	0x20	
#define PCI9118_GAIN	0x24	
#define PCI9118_BURST	0x28	
#define PCI9118_SCANMOD	0x2c	
#define PCI9118_ADFUNC	0x30	
#define PCI9118_DELFIFO	0x34	
#define PCI9118_INTSRC	0x38	
#define PCI9118_INTCTRL	0x38	

#define AdControl_UniP	0x80	
#define AdControl_Diff	0x40	
#define AdControl_SoftG	0x20	
#define	AdControl_ExtG	0x10	
#define AdControl_ExtM	0x08	
#define AdControl_TmrTr	0x04	
#define AdControl_Int	0x02	
#define AdControl_Dma	0x01	

#define AdFunction_PDTrg	0x80	
#define AdFunction_PETrg	0x40	
#define AdFunction_BSSH		0x20	
#define AdFunction_BM		0x10	
#define AdFunction_BS		0x08	
#define AdFunction_PM		0x04	
#define AdFunction_AM		0x02	
#define AdFunction_Start	0x01	

#define AdStatus_nFull	0x100	
#define AdStatus_nHfull	0x080	
#define AdStatus_nEpty	0x040	
#define AdStatus_Acmp	0x020	
#define AdStatus_DTH	0x010	
#define AdStatus_Bover	0x008	
#define AdStatus_ADOS	0x004	
#define AdStatus_ADOR	0x002	
#define AdStatus_ADrdy	0x001	

#define Int_Timer	0x08	
#define Int_About	0x04	
#define Int_Hfull	0x02	
#define Int_DTrg	0x01	

#define START_AI_EXT	0x01	
#define STOP_AI_EXT	0x02	
#define START_AI_INT	0x04	
#define STOP_AI_INT	0x08	

#define EXTTRG_AI	0	

static const struct comedi_lrange range_pci9118dg_hr = { 8, {
							     BIP_RANGE(5),
							     BIP_RANGE(2.5),
							     BIP_RANGE(1.25),
							     BIP_RANGE(0.625),
							     UNI_RANGE(10),
							     UNI_RANGE(5),
							     UNI_RANGE(2.5),
							     UNI_RANGE(1.25)
							     }
};

static const struct comedi_lrange range_pci9118hg = { 8, {
							  BIP_RANGE(5),
							  BIP_RANGE(0.5),
							  BIP_RANGE(0.05),
							  BIP_RANGE(0.005),
							  UNI_RANGE(10),
							  UNI_RANGE(1),
							  UNI_RANGE(0.1),
							  UNI_RANGE(0.01)
							  }
};

#define PCI9118_BIPOLAR_RANGES	4	

static int pci9118_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int pci9118_detach(struct comedi_device *dev);

struct boardtype {
	const char *name;		
	int vendor_id;			
	int device_id;
	int iorange_amcc;		
	int iorange_9118;		
	int n_aichan;			
	int n_aichand;			
	int mux_aichan;			
	int n_aichanlist;		
	int n_aochan;			
	int ai_maxdata;			
	int ao_maxdata;			
	const struct comedi_lrange *rangelist_ai;	
	const struct comedi_lrange *rangelist_ao;	
	unsigned int ai_ns_min;		
	unsigned int ai_pacer_min;	
	int half_fifo_size;		

};

static DEFINE_PCI_DEVICE_TABLE(pci9118_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMCC, 0x80d9) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, pci9118_pci_table);

static const struct boardtype boardtypes[] = {
	{"pci9118dg", PCI_VENDOR_ID_AMCC, 0x80d9,
	 AMCC_OP_REG_SIZE, IORANGE_9118,
	 16, 8, 256, PCI9118_CHANLEN, 2, 0x0fff, 0x0fff,
	 &range_pci9118dg_hr, &range_bipolar10,
	 3000, 12, 512},
	{"pci9118hg", PCI_VENDOR_ID_AMCC, 0x80d9,
	 AMCC_OP_REG_SIZE, IORANGE_9118,
	 16, 8, 256, PCI9118_CHANLEN, 2, 0x0fff, 0x0fff,
	 &range_pci9118hg, &range_bipolar10,
	 3000, 12, 512},
	{"pci9118hr", PCI_VENDOR_ID_AMCC, 0x80d9,
	 AMCC_OP_REG_SIZE, IORANGE_9118,
	 16, 8, 256, PCI9118_CHANLEN, 2, 0xffff, 0x0fff,
	 &range_pci9118dg_hr, &range_bipolar10,
	 10000, 40, 512},
};

#define n_boardtypes (sizeof(boardtypes)/sizeof(struct boardtype))

static struct comedi_driver driver_pci9118 = {
	.driver_name = "adl_pci9118",
	.module = THIS_MODULE,
	.attach = pci9118_attach,
	.detach = pci9118_detach,
	.num_names = n_boardtypes,
	.board_name = &boardtypes[0].name,
	.offset = sizeof(struct boardtype),
};

static int __devinit driver_pci9118_pci_probe(struct pci_dev *dev,
					      const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, driver_pci9118.driver_name);
}

static void __devexit driver_pci9118_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_pci9118_pci_driver = {
	.id_table = pci9118_pci_table,
	.probe = &driver_pci9118_pci_probe,
	.remove = __devexit_p(&driver_pci9118_pci_remove)
};

static int __init driver_pci9118_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_pci9118);
	if (retval < 0)
		return retval;

	driver_pci9118_pci_driver.name = (char *)driver_pci9118.driver_name;
	return pci_register_driver(&driver_pci9118_pci_driver);
}

static void __exit driver_pci9118_cleanup_module(void)
{
	pci_unregister_driver(&driver_pci9118_pci_driver);
	comedi_driver_unregister(&driver_pci9118);
}

module_init(driver_pci9118_init_module);
module_exit(driver_pci9118_cleanup_module);

struct pci9118_private {
	unsigned long iobase_a;	
	unsigned int master;	
	struct pci_dev *pcidev;	
	unsigned int usemux;	
#ifdef PCI9118_PARANOIDCHECK
	unsigned short chanlist[PCI9118_CHANLEN + 1];	
	unsigned char chanlistlen;	
#endif
	unsigned char AdControlReg;	
	unsigned char IntControlReg;	
	unsigned char AdFunctionReg;	
	char valid;			
	char ai_neverending;		
	unsigned int i8254_osc_base;	
	unsigned int ai_do;		
	unsigned int ai_act_scan;	
	unsigned int ai_buf_ptr;	
	unsigned int ai_n_chan;		
	unsigned int ai_n_scanlen;	
	unsigned int ai_n_realscanlen;	
	unsigned int ai_act_dmapos;	
	unsigned int ai_add_front;	
	unsigned int ai_add_back;	
	unsigned int *ai_chanlist;	
	unsigned int ai_timer1;
	unsigned int ai_timer2;
	unsigned int ai_flags;
	char ai12_startstop;		
	unsigned int ai_divisor1, ai_divisor2;	
	unsigned int ai_data_len;
	short *ai_data;
	short ao_data[2];			
	unsigned int ai_scans;			
	char dma_doublebuf;			
	unsigned int dma_actbuf;		
	short *dmabuf_virt[2];			
	unsigned long dmabuf_hw[2];		
	unsigned int dmabuf_size[2];		
	unsigned int dmabuf_use_size[2];	
	unsigned int dmabuf_used_size[2];	
	unsigned int dmabuf_panic_size[2];
	unsigned int dmabuf_samples[2];		
	int dmabuf_pages[2];			
	unsigned char cnt0_users;		
	unsigned char exttrg_users;		
	unsigned int cnt0_divisor;		
	void (*int_ai_func) (struct comedi_device *, struct comedi_subdevice *,
		unsigned short,
		unsigned int,
		unsigned short);	
	unsigned char ai16bits;		
	unsigned char usedma;		
	unsigned char useeoshandle;	
	unsigned char usessh;		
	int softsshdelay;		
	unsigned char softsshsample;	
	unsigned char softsshhold;	
	unsigned int ai_maskerr;	
	unsigned int ai_maskharderr;	
	unsigned int ai_inttrig_start;	
};

#define devpriv ((struct pci9118_private *)dev->private)
#define this_board ((struct boardtype *)dev->board_ptr)


static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s, int n_chan,
			      unsigned int *chanlist, int frontadd,
			      int backadd);
static int setup_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s, int n_chan,
			      unsigned int *chanlist, int rot, int frontadd,
			      int backadd, int usedma, char eoshandle);
static void start_pacer(struct comedi_device *dev, int mode,
			unsigned int divisor1, unsigned int divisor2);
static int pci9118_reset(struct comedi_device *dev);
static int pci9118_exttrg_add(struct comedi_device *dev, unsigned char source);
static int pci9118_exttrg_del(struct comedi_device *dev, unsigned char source);
static int pci9118_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s);
static void pci9118_calc_divisors(char mode, struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int *tim1, unsigned int *tim2,
				  unsigned int flags, int chans,
				  unsigned int *div1, unsigned int *div2,
				  char usessh, unsigned int chnsshfront);

static int pci9118_insn_read_ai(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{

	int n, timeout;

	devpriv->AdControlReg = AdControl_Int & 0xff;
	devpriv->AdFunctionReg = AdFunction_PDTrg | AdFunction_PETrg;
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);

	if (!setup_channel_list(dev, s, 1, &insn->chanspec, 0, 0, 0, 0, 0))
		return -EINVAL;

	outl(0, dev->iobase + PCI9118_DELFIFO);	

	for (n = 0; n < insn->n; n++) {
		outw(0, dev->iobase + PCI9118_SOFTTRG);	
		udelay(2);
		timeout = 100;
		while (timeout--) {
			if (inl(dev->iobase + PCI9118_ADSTAT) & AdStatus_ADrdy)
				goto conv_finish;
			udelay(1);
		}

		comedi_error(dev, "A/D insn timeout");
		data[n] = 0;
		outl(0, dev->iobase + PCI9118_DELFIFO);	
		return -ETIME;

conv_finish:
		if (devpriv->ai16bits) {
			data[n] =
			    (inl(dev->iobase +
				 PCI9118_AD_DATA) & 0xffff) ^ 0x8000;
		} else {
			data[n] =
			    (inw(dev->iobase + PCI9118_AD_DATA) >> 4) & 0xfff;
		}
	}

	outl(0, dev->iobase + PCI9118_DELFIFO);	
	return n;

}

static int pci9118_insn_write_ao(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int n, chanreg, ch;

	ch = CR_CHAN(insn->chanspec);
	if (ch)
		chanreg = PCI9118_DA2;
	else
		chanreg = PCI9118_DA1;


	for (n = 0; n < insn->n; n++) {
		outl(data[n], dev->iobase + chanreg);
		devpriv->ao_data[ch] = data[n];
	}

	return n;
}

static int pci9118_insn_read_ao(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int n, chan;

	chan = CR_CHAN(insn->chanspec);
	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_data[chan];

	return n;
}

static int pci9118_insn_bits_di(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	data[1] = inl(dev->iobase + PCI9118_DI) & 0xf;

	return 2;
}

static int pci9118_insn_bits_do(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		outl(s->state & 0x0f, dev->iobase + PCI9118_DO);
	}
	data[1] = s->state;

	return 2;
}

static void interrupt_pci9118_ai_mode4_switch(struct comedi_device *dev)
{
	devpriv->AdFunctionReg =
	    AdFunction_PDTrg | AdFunction_PETrg | AdFunction_AM;
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
	outl(0x30, dev->iobase + PCI9118_CNTCTRL);
	outl((devpriv->dmabuf_hw[1 - devpriv->dma_actbuf] >> 1) & 0xff,
	     dev->iobase + PCI9118_CNT0);
	outl((devpriv->dmabuf_hw[1 - devpriv->dma_actbuf] >> 9) & 0xff,
	     dev->iobase + PCI9118_CNT0);
	devpriv->AdFunctionReg |= AdFunction_Start;
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
}

static unsigned int defragment_dma_buffer(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  short *dma_buffer,
					  unsigned int num_samples)
{
	unsigned int i = 0, j = 0;
	unsigned int start_pos = devpriv->ai_add_front,
	    stop_pos = devpriv->ai_add_front + devpriv->ai_n_chan;
	unsigned int raw_scanlen = devpriv->ai_add_front + devpriv->ai_n_chan +
	    devpriv->ai_add_back;

	for (i = 0; i < num_samples; i++) {
		if (devpriv->ai_act_dmapos >= start_pos &&
		    devpriv->ai_act_dmapos < stop_pos) {
			dma_buffer[j++] = dma_buffer[i];
		}
		devpriv->ai_act_dmapos++;
		devpriv->ai_act_dmapos %= raw_scanlen;
	}

	return j;
}

static int move_block_from_dma(struct comedi_device *dev,
					struct comedi_subdevice *s,
					short *dma_buffer,
					unsigned int num_samples)
{
	unsigned int num_bytes;

	num_samples = defragment_dma_buffer(dev, s, dma_buffer, num_samples);
	devpriv->ai_act_scan +=
	    (s->async->cur_chan + num_samples) / devpriv->ai_n_scanlen;
	s->async->cur_chan += num_samples;
	s->async->cur_chan %= devpriv->ai_n_scanlen;
	num_bytes =
	    cfc_write_array_to_buffer(s, dma_buffer,
				      num_samples * sizeof(short));
	if (num_bytes < num_samples * sizeof(short))
		return -1;
	return 0;
}

static char pci9118_decode_error_status(struct comedi_device *dev,
					struct comedi_subdevice *s,
					unsigned char m)
{
	if (m & 0x100) {
		comedi_error(dev, "A/D FIFO Full status (Fatal Error!)");
		devpriv->ai_maskerr &= ~0x100L;
	}
	if (m & 0x008) {
		comedi_error(dev,
			     "A/D Burst Mode Overrun Status (Fatal Error!)");
		devpriv->ai_maskerr &= ~0x008L;
	}
	if (m & 0x004) {
		comedi_error(dev, "A/D Over Speed Status (Warning!)");
		devpriv->ai_maskerr &= ~0x004L;
	}
	if (m & 0x002) {
		comedi_error(dev, "A/D Overrun Status (Fatal Error!)");
		devpriv->ai_maskerr &= ~0x002L;
	}
	if (m & devpriv->ai_maskharderr) {
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		pci9118_ai_cancel(dev, s);
		comedi_event(dev, s);
		return 1;
	}

	return 0;
}

static void pci9118_ai_munge(struct comedi_device *dev,
			     struct comedi_subdevice *s, void *data,
			     unsigned int num_bytes,
			     unsigned int start_chan_index)
{
	unsigned int i, num_samples = num_bytes / sizeof(short);
	short *array = data;

	for (i = 0; i < num_samples; i++) {
		if (devpriv->usedma)
			array[i] = be16_to_cpu(array[i]);
		if (devpriv->ai16bits)
			array[i] ^= 0x8000;
		else
			array[i] = (array[i] >> 4) & 0x0fff;

	}
}

static void interrupt_pci9118_ai_onesample(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   unsigned short int_adstat,
					   unsigned int int_amcc,
					   unsigned short int_daq)
{
	register short sampl;

	s->async->events = 0;

	if (int_adstat & devpriv->ai_maskerr)
		if (pci9118_decode_error_status(dev, s, int_adstat))
			return;

	sampl = inw(dev->iobase + PCI9118_AD_DATA);

#ifdef PCI9118_PARANOIDCHECK
	if (devpriv->ai16bits == 0) {
		if ((sampl & 0x000f) != devpriv->chanlist[s->async->cur_chan]) {
							
			printk
			    ("comedi: A/D  SAMPL - data dropout: "
				"received channel %d, expected %d!\n",
				sampl & 0x000f,
				devpriv->chanlist[s->async->cur_chan]);
			s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
			pci9118_ai_cancel(dev, s);
			comedi_event(dev, s);
			return;
		}
	}
#endif
	cfc_write_to_buffer(s, sampl);
	s->async->cur_chan++;
	if (s->async->cur_chan >= devpriv->ai_n_scanlen) {
							
		s->async->cur_chan %= devpriv->ai_n_scanlen;
		devpriv->ai_act_scan++;
		if (!(devpriv->ai_neverending))
			if (devpriv->ai_act_scan >= devpriv->ai_scans) {
							
				pci9118_ai_cancel(dev, s);
				s->async->events |= COMEDI_CB_EOA;
			}
	}

	if (s->async->events)
		comedi_event(dev, s);
}

static void interrupt_pci9118_ai_dma(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned short int_adstat,
				     unsigned int int_amcc,
				     unsigned short int_daq)
{
	unsigned int next_dma_buf, samplesinbuf, sampls, m;

	if (int_amcc & MASTER_ABORT_INT) {
		comedi_error(dev, "AMCC IRQ - MASTER DMA ABORT!");
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		pci9118_ai_cancel(dev, s);
		comedi_event(dev, s);
		return;
	}

	if (int_amcc & TARGET_ABORT_INT) {
		comedi_error(dev, "AMCC IRQ - TARGET DMA ABORT!");
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		pci9118_ai_cancel(dev, s);
		comedi_event(dev, s);
		return;
	}
	if (int_adstat & devpriv->ai_maskerr)
					
		if (pci9118_decode_error_status(dev, s, int_adstat))
			return;

	samplesinbuf = devpriv->dmabuf_use_size[devpriv->dma_actbuf] >> 1;
					

	if (devpriv->dma_doublebuf) {	
		next_dma_buf = 1 - devpriv->dma_actbuf;
		outl(devpriv->dmabuf_hw[next_dma_buf],
		     devpriv->iobase_a + AMCC_OP_REG_MWAR);
		outl(devpriv->dmabuf_use_size[next_dma_buf],
		     devpriv->iobase_a + AMCC_OP_REG_MWTC);
		devpriv->dmabuf_used_size[next_dma_buf] =
		    devpriv->dmabuf_use_size[next_dma_buf];
		if (devpriv->ai_do == 4)
			interrupt_pci9118_ai_mode4_switch(dev);
	}

	if (samplesinbuf) {
		m = devpriv->ai_data_len >> 1;	
		sampls = m;
		move_block_from_dma(dev, s,
				    devpriv->dmabuf_virt[devpriv->dma_actbuf],
				    samplesinbuf);
		m = m - sampls;		
	}

	if (!devpriv->ai_neverending)
		if (devpriv->ai_act_scan >= devpriv->ai_scans) {
							
			pci9118_ai_cancel(dev, s);
			s->async->events |= COMEDI_CB_EOA;
		}

	if (devpriv->dma_doublebuf) {	
		devpriv->dma_actbuf = 1 - devpriv->dma_actbuf;
	} else {	
		outl(devpriv->dmabuf_hw[0],
		     devpriv->iobase_a + AMCC_OP_REG_MWAR);
		outl(devpriv->dmabuf_use_size[0],
		     devpriv->iobase_a + AMCC_OP_REG_MWTC);
		if (devpriv->ai_do == 4)
			interrupt_pci9118_ai_mode4_switch(dev);
	}

	comedi_event(dev, s);
}

static irqreturn_t interrupt_pci9118(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned int int_daq = 0, int_amcc, int_adstat;

	if (!dev->attached)
		return IRQ_NONE;	

	int_daq = inl(dev->iobase + PCI9118_INTSRC) & 0xf;
					
	int_amcc = inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR);
					


	if ((!int_daq) && (!(int_amcc & ANY_S593X_INT)))
		return IRQ_NONE;	

	outl(int_amcc | 0x00ff0000, devpriv->iobase_a + AMCC_OP_REG_INTCSR);
					

	int_adstat = inw(dev->iobase + PCI9118_ADSTAT) & 0x1ff;
					

	if (devpriv->ai_do) {
		if (devpriv->ai12_startstop)
			if ((int_adstat & AdStatus_DTH) &&
							(int_daq & Int_DTrg)) {
						
				if (devpriv->ai12_startstop & START_AI_EXT) {
					devpriv->ai12_startstop &=
					    ~START_AI_EXT;
					if (!(devpriv->ai12_startstop &
							STOP_AI_EXT))
							pci9118_exttrg_del
							(dev, EXTTRG_AI);
						
					start_pacer(dev, devpriv->ai_do,
						devpriv->ai_divisor1,
						devpriv->ai_divisor2);
						
					outl(devpriv->AdControlReg,
						dev->iobase + PCI9118_ADCNTRL);
				} else {
					if (devpriv->ai12_startstop &
						STOP_AI_EXT) {
						devpriv->ai12_startstop &=
							~STOP_AI_EXT;
						pci9118_exttrg_del
							(dev, EXTTRG_AI);
						
						devpriv->ai_neverending = 0;
					}
				}
			}

		(devpriv->int_ai_func) (dev, dev->subdevices + 0, int_adstat,
					int_amcc, int_daq);

	}
	return IRQ_HANDLED;
}

static int pci9118_ai_inttrig(struct comedi_device *dev,
			      struct comedi_subdevice *s, unsigned int trignum)
{
	if (trignum != devpriv->ai_inttrig_start)
		return -EINVAL;

	devpriv->ai12_startstop &= ~START_AI_INT;
	s->async->inttrig = NULL;

	outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
	if (devpriv->ai_do != 3) {
		start_pacer(dev, devpriv->ai_do, devpriv->ai_divisor1,
			    devpriv->ai_divisor2);
		devpriv->AdControlReg |= AdControl_SoftG;
	}
	outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);

	return 1;
}

static int pci9118_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;
	unsigned int divisor1 = 0, divisor2 = 0;

	

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW | TRIG_EXT | TRIG_INT;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	if (devpriv->master)
		cmd->scan_begin_src &= TRIG_TIMER | TRIG_EXT | TRIG_FOLLOW;
	else
		cmd->scan_begin_src &= TRIG_FOLLOW;

	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	if (devpriv->master)
		cmd->convert_src &= TRIG_TIMER | TRIG_EXT | TRIG_NOW;
	else
		cmd->convert_src &= TRIG_TIMER | TRIG_EXT;

	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE | TRIG_EXT;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;


	if (cmd->start_src != TRIG_NOW &&
	    cmd->start_src != TRIG_INT && cmd->start_src != TRIG_EXT) {
		cmd->start_src = TRIG_NOW;
		err++;
	}

	if (cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->scan_begin_src != TRIG_EXT &&
	    cmd->scan_begin_src != TRIG_INT &&
	    cmd->scan_begin_src != TRIG_FOLLOW) {
		cmd->scan_begin_src = TRIG_FOLLOW;
		err++;
	}

	if (cmd->convert_src != TRIG_TIMER &&
	    cmd->convert_src != TRIG_EXT && cmd->convert_src != TRIG_NOW) {
		cmd->convert_src = TRIG_TIMER;
		err++;
	}

	if (cmd->scan_end_src != TRIG_COUNT) {
		cmd->scan_end_src = TRIG_COUNT;
		err++;
	}

	if (cmd->stop_src != TRIG_NONE &&
	    cmd->stop_src != TRIG_COUNT &&
	    cmd->stop_src != TRIG_INT && cmd->stop_src != TRIG_EXT) {
		cmd->stop_src = TRIG_COUNT;
		err++;
	}

	if (cmd->start_src == TRIG_EXT && cmd->scan_begin_src == TRIG_EXT) {
		cmd->start_src = TRIG_NOW;
		err++;
	}

	if (cmd->start_src == TRIG_INT && cmd->scan_begin_src == TRIG_INT) {
		cmd->start_src = TRIG_NOW;
		err++;
	}

	if ((cmd->scan_begin_src & (TRIG_TIMER | TRIG_EXT)) &&
	    (!(cmd->convert_src & (TRIG_TIMER | TRIG_NOW)))) {
		cmd->convert_src = TRIG_TIMER;
		err++;
	}

	if ((cmd->scan_begin_src == TRIG_FOLLOW) &&
	    (!(cmd->convert_src & (TRIG_TIMER | TRIG_EXT)))) {
		cmd->convert_src = TRIG_TIMER;
		err++;
	}

	if (cmd->stop_src == TRIG_EXT && cmd->scan_begin_src == TRIG_EXT) {
		cmd->stop_src = TRIG_COUNT;
		err++;
	}

	if (err)
		return 2;

	

	if (cmd->start_src & (TRIG_NOW | TRIG_EXT))
		if (cmd->start_arg != 0) {
			cmd->start_arg = 0;
			err++;
		}

	if (cmd->scan_begin_src & (TRIG_FOLLOW | TRIG_EXT))
		if (cmd->scan_begin_arg != 0) {
			cmd->scan_begin_arg = 0;
			err++;
		}

	if ((cmd->scan_begin_src == TRIG_TIMER) &&
	    (cmd->convert_src == TRIG_TIMER) && (cmd->scan_end_arg == 1)) {
		cmd->scan_begin_src = TRIG_FOLLOW;
		cmd->convert_arg = cmd->scan_begin_arg;
		cmd->scan_begin_arg = 0;
	}

	if (cmd->scan_begin_src == TRIG_TIMER)
		if (cmd->scan_begin_arg < this_board->ai_ns_min) {
			cmd->scan_begin_arg = this_board->ai_ns_min;
			err++;
		}

	if (cmd->scan_begin_src == TRIG_EXT)
		if (cmd->scan_begin_arg) {
			cmd->scan_begin_arg = 0;
			err++;
			if (cmd->scan_end_arg > 65535) {
				cmd->scan_end_arg = 65535;
				err++;
			}
		}

	if (cmd->convert_src & (TRIG_TIMER | TRIG_NOW))
		if (cmd->convert_arg < this_board->ai_ns_min) {
			cmd->convert_arg = this_board->ai_ns_min;
			err++;
		}

	if (cmd->convert_src == TRIG_EXT)
		if (cmd->convert_arg) {
			cmd->convert_arg = 0;
			err++;
		}

	if (cmd->stop_src == TRIG_COUNT) {
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
	} else {		
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 1;
		err++;
	}

	if (cmd->chanlist_len > this_board->n_aichanlist) {
		cmd->chanlist_len = this_board->n_aichanlist;
		err++;
	}

	if (cmd->scan_end_arg < cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	if ((cmd->scan_end_arg % cmd->chanlist_len)) {
		cmd->scan_end_arg =
		    cmd->chanlist_len * (cmd->scan_end_arg / cmd->chanlist_len);
		err++;
	}

	if (err)
		return 3;

	

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer(devpriv->i8254_osc_base, &divisor1,
					  &divisor2, &cmd->scan_begin_arg,
					  cmd->flags & TRIG_ROUND_MASK);
		if (cmd->scan_begin_arg < this_board->ai_ns_min)
			cmd->scan_begin_arg = this_board->ai_ns_min;
		if (tmp != cmd->scan_begin_arg)
			err++;
	}

	if (cmd->convert_src & (TRIG_TIMER | TRIG_NOW)) {
		tmp = cmd->convert_arg;
		i8253_cascade_ns_to_timer(devpriv->i8254_osc_base, &divisor1,
					  &divisor2, &cmd->convert_arg,
					  cmd->flags & TRIG_ROUND_MASK);
		if (cmd->convert_arg < this_board->ai_ns_min)
			cmd->convert_arg = this_board->ai_ns_min;
		if (tmp != cmd->convert_arg)
			err++;
		if (cmd->scan_begin_src == TRIG_TIMER
		    && cmd->convert_src == TRIG_NOW) {
			if (cmd->convert_arg == 0) {
				if (cmd->scan_begin_arg <
				    this_board->ai_ns_min *
				    (cmd->scan_end_arg + 2)) {
					cmd->scan_begin_arg =
					    this_board->ai_ns_min *
					    (cmd->scan_end_arg + 2);
					err++;
				}
			} else {
				if (cmd->scan_begin_arg <
				    cmd->convert_arg * cmd->chanlist_len) {
					cmd->scan_begin_arg =
					    cmd->convert_arg *
					    cmd->chanlist_len;
					err++;
				}
			}
		}
	}

	if (err)
		return 4;

	if (cmd->chanlist)
		if (!check_channel_list(dev, s, cmd->chanlist_len,
					cmd->chanlist, 0, 0))
			return 5;	

	return 0;
}

static int Compute_and_setup_dma(struct comedi_device *dev)
{
	unsigned int dmalen0, dmalen1, i;

	DPRINTK("adl_pci9118 EDBG: BGN: Compute_and_setup_dma()\n");
	dmalen0 = devpriv->dmabuf_size[0];
	dmalen1 = devpriv->dmabuf_size[1];
	DPRINTK("1 dmalen0=%d dmalen1=%d ai_data_len=%d\n", dmalen0, dmalen1,
		devpriv->ai_data_len);
	
	if (dmalen0 > (devpriv->ai_data_len)) {
		dmalen0 = devpriv->ai_data_len & ~3L;	
	}
	if (dmalen1 > (devpriv->ai_data_len)) {
		dmalen1 = devpriv->ai_data_len & ~3L;	
	}
	DPRINTK("2 dmalen0=%d dmalen1=%d\n", dmalen0, dmalen1);

	
	if (devpriv->ai_flags & TRIG_WAKE_EOS) {
		if (dmalen0 < (devpriv->ai_n_realscanlen << 1)) {
			
			devpriv->ai_flags &= (~TRIG_WAKE_EOS);
			printk
			    ("comedi%d: WAR: DMA0 buf too short, can't "
					"support TRIG_WAKE_EOS (%d<%d)\n",
			     dev->minor, dmalen0,
			     devpriv->ai_n_realscanlen << 1);
		} else {
			
			dmalen0 = devpriv->ai_n_realscanlen << 1;
			DPRINTK
				("21 dmalen0=%d ai_n_realscanlen=%d "
							"useeoshandle=%d\n",
				dmalen0, devpriv->ai_n_realscanlen,
				devpriv->useeoshandle);
			if (devpriv->useeoshandle)
				dmalen0 += 2;
			if (dmalen0 < 4) {
				printk
					("comedi%d: ERR: DMA0 buf len bug? "
								"(%d<4)\n",
					dev->minor, dmalen0);
				dmalen0 = 4;
			}
		}
	}
	if (devpriv->ai_flags & TRIG_WAKE_EOS) {
		if (dmalen1 < (devpriv->ai_n_realscanlen << 1)) {
			
			devpriv->ai_flags &= (~TRIG_WAKE_EOS);
			printk
			    ("comedi%d: WAR: DMA1 buf too short, "
					"can't support TRIG_WAKE_EOS (%d<%d)\n",
			     dev->minor, dmalen1,
			     devpriv->ai_n_realscanlen << 1);
		} else {
			
			dmalen1 = devpriv->ai_n_realscanlen << 1;
			DPRINTK
			    ("22 dmalen1=%d ai_n_realscanlen=%d "
							"useeoshandle=%d\n",
			     dmalen1, devpriv->ai_n_realscanlen,
			     devpriv->useeoshandle);
			if (devpriv->useeoshandle)
				dmalen1 -= 2;
			if (dmalen1 < 4) {
				printk
					("comedi%d: ERR: DMA1 buf len bug? "
								"(%d<4)\n",
					dev->minor, dmalen1);
				dmalen1 = 4;
			}
		}
	}

	DPRINTK("3 dmalen0=%d dmalen1=%d\n", dmalen0, dmalen1);
	
	if (!(devpriv->ai_flags & TRIG_WAKE_EOS)) {
		
		i = dmalen0;
		dmalen0 =
		    (dmalen0 / (devpriv->ai_n_realscanlen << 1)) *
		    (devpriv->ai_n_realscanlen << 1);
		dmalen0 &= ~3L;
		if (!dmalen0)
			dmalen0 = i;	
		i = dmalen1;
		dmalen1 =
		    (dmalen1 / (devpriv->ai_n_realscanlen << 1)) *
		    (devpriv->ai_n_realscanlen << 1);
		dmalen1 &= ~3L;
		if (!dmalen1)
			dmalen1 = i;	
		if (!devpriv->ai_neverending) {
			
			if (dmalen0 >
			    ((devpriv->ai_n_realscanlen << 1) *
			     devpriv->ai_scans)) {
				DPRINTK
				    ("3.0 ai_n_realscanlen=%d ai_scans=%d\n",
				     devpriv->ai_n_realscanlen,
				     devpriv->ai_scans);
				dmalen0 =
				    (devpriv->ai_n_realscanlen << 1) *
				    devpriv->ai_scans;
				DPRINTK("3.1 dmalen0=%d dmalen1=%d\n", dmalen0,
					dmalen1);
				dmalen0 &= ~3L;
			} else {	
				if (dmalen1 >
				    ((devpriv->ai_n_realscanlen << 1) *
				     devpriv->ai_scans - dmalen0))
					dmalen1 =
					    (devpriv->ai_n_realscanlen << 1) *
					    devpriv->ai_scans - dmalen0;
				DPRINTK("3.2 dmalen0=%d dmalen1=%d\n", dmalen0,
					dmalen1);
				dmalen1 &= ~3L;
			}
		}
	}

	DPRINTK("4 dmalen0=%d dmalen1=%d\n", dmalen0, dmalen1);

	
	devpriv->dma_actbuf = 0;
	devpriv->dmabuf_use_size[0] = dmalen0;
	devpriv->dmabuf_use_size[1] = dmalen1;

	DPRINTK("5 dmalen0=%d dmalen1=%d\n", dmalen0, dmalen1);
#if 0
	if (devpriv->ai_n_scanlen < this_board->half_fifo_size) {
		devpriv->dmabuf_panic_size[0] =
		    (this_board->half_fifo_size / devpriv->ai_n_scanlen +
		     1) * devpriv->ai_n_scanlen * sizeof(short);
		devpriv->dmabuf_panic_size[1] =
		    (this_board->half_fifo_size / devpriv->ai_n_scanlen +
		     1) * devpriv->ai_n_scanlen * sizeof(short);
	} else {
		devpriv->dmabuf_panic_size[0] =
		    (devpriv->ai_n_scanlen << 1) % devpriv->dmabuf_size[0];
		devpriv->dmabuf_panic_size[1] =
		    (devpriv->ai_n_scanlen << 1) % devpriv->dmabuf_size[1];
	}
#endif

	outl(inl(devpriv->iobase_a + AMCC_OP_REG_MCSR) & (~EN_A2P_TRANSFERS),
			devpriv->iobase_a + AMCC_OP_REG_MCSR);	
	outl(devpriv->dmabuf_hw[0], devpriv->iobase_a + AMCC_OP_REG_MWAR);
	outl(devpriv->dmabuf_use_size[0], devpriv->iobase_a + AMCC_OP_REG_MWTC);
	
	outl(0x00000000 | AINT_WRITE_COMPL,
	     devpriv->iobase_a + AMCC_OP_REG_INTCSR);

	outl(inl(devpriv->iobase_a +
		 AMCC_OP_REG_MCSR) | RESET_A2P_FLAGS | A2P_HI_PRIORITY |
	     EN_A2P_TRANSFERS, devpriv->iobase_a + AMCC_OP_REG_MCSR);
	outl(inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR) | EN_A2P_TRANSFERS,
			devpriv->iobase_a + AMCC_OP_REG_INTCSR);
						

	DPRINTK("adl_pci9118 EDBG: END: Compute_and_setup_dma()\n");
	return 0;
}

static int pci9118_ai_docmd_sampl(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	DPRINTK("adl_pci9118 EDBG: BGN: pci9118_ai_docmd_sampl(%d,) [%d]\n",
		dev->minor, devpriv->ai_do);
	switch (devpriv->ai_do) {
	case 1:
		devpriv->AdControlReg |= AdControl_TmrTr;
		break;
	case 2:
		comedi_error(dev, "pci9118_ai_docmd_sampl() mode 2 bug!\n");
		return -EIO;
	case 3:
		devpriv->AdControlReg |= AdControl_ExtM;
		break;
	case 4:
		comedi_error(dev, "pci9118_ai_docmd_sampl() mode 4 bug!\n");
		return -EIO;
	default:
		comedi_error(dev,
			     "pci9118_ai_docmd_sampl() mode number bug!\n");
		return -EIO;
	}

	devpriv->int_ai_func = interrupt_pci9118_ai_onesample;
						

	if (devpriv->ai12_startstop)
		pci9118_exttrg_add(dev, EXTTRG_AI);
						

	if ((devpriv->ai_do == 1) || (devpriv->ai_do == 2))
		devpriv->IntControlReg |= Int_Timer;

	devpriv->AdControlReg |= AdControl_Int;

	outl(inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR) | 0x1f00,
			devpriv->iobase_a + AMCC_OP_REG_INTCSR);
							

	if (!(devpriv->ai12_startstop & (START_AI_EXT | START_AI_INT))) {
		outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
		outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
		if (devpriv->ai_do != 3) {
			start_pacer(dev, devpriv->ai_do, devpriv->ai_divisor1,
				    devpriv->ai_divisor2);
			devpriv->AdControlReg |= AdControl_SoftG;
		}
		outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
	}

	DPRINTK("adl_pci9118 EDBG: END: pci9118_ai_docmd_sampl()\n");
	return 0;
}

static int pci9118_ai_docmd_dma(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	DPRINTK("adl_pci9118 EDBG: BGN: pci9118_ai_docmd_dma(%d,) [%d,%d]\n",
		dev->minor, devpriv->ai_do, devpriv->usedma);
	Compute_and_setup_dma(dev);

	switch (devpriv->ai_do) {
	case 1:
		devpriv->AdControlReg |=
		    ((AdControl_TmrTr | AdControl_Dma) & 0xff);
		break;
	case 2:
		devpriv->AdControlReg |=
		    ((AdControl_TmrTr | AdControl_Dma) & 0xff);
		devpriv->AdFunctionReg =
		    AdFunction_PDTrg | AdFunction_PETrg | AdFunction_BM |
		    AdFunction_BS;
		if (devpriv->usessh && (!devpriv->softsshdelay))
			devpriv->AdFunctionReg |= AdFunction_BSSH;
		outl(devpriv->ai_n_realscanlen, dev->iobase + PCI9118_BURST);
		break;
	case 3:
		devpriv->AdControlReg |=
		    ((AdControl_ExtM | AdControl_Dma) & 0xff);
		devpriv->AdFunctionReg = AdFunction_PDTrg | AdFunction_PETrg;
		break;
	case 4:
		devpriv->AdControlReg |=
		    ((AdControl_TmrTr | AdControl_Dma) & 0xff);
		devpriv->AdFunctionReg =
		    AdFunction_PDTrg | AdFunction_PETrg | AdFunction_AM;
		outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
		outl(0x30, dev->iobase + PCI9118_CNTCTRL);
		outl((devpriv->dmabuf_hw[0] >> 1) & 0xff,
		     dev->iobase + PCI9118_CNT0);
		outl((devpriv->dmabuf_hw[0] >> 9) & 0xff,
		     dev->iobase + PCI9118_CNT0);
		devpriv->AdFunctionReg |= AdFunction_Start;
		break;
	default:
		comedi_error(dev, "pci9118_ai_docmd_dma() mode number bug!\n");
		return -EIO;
	}

	if (devpriv->ai12_startstop) {
		pci9118_exttrg_add(dev, EXTTRG_AI);
						
	}

	devpriv->int_ai_func = interrupt_pci9118_ai_dma;
						

	outl(0x02000000 | AINT_WRITE_COMPL,
	     devpriv->iobase_a + AMCC_OP_REG_INTCSR);

	if (!(devpriv->ai12_startstop & (START_AI_EXT | START_AI_INT))) {
		outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
		outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
		if (devpriv->ai_do != 3) {
			start_pacer(dev, devpriv->ai_do, devpriv->ai_divisor1,
				    devpriv->ai_divisor2);
			devpriv->AdControlReg |= AdControl_SoftG;
		}
		outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);
	}

	DPRINTK("adl_pci9118 EDBG: BGN: pci9118_ai_docmd_dma()\n");
	return 0;
}

static int pci9118_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int addchans = 0;
	int ret = 0;

	DPRINTK("adl_pci9118 EDBG: BGN: pci9118_ai_cmd(%d,)\n", dev->minor);
	devpriv->ai12_startstop = 0;
	devpriv->ai_flags = cmd->flags;
	devpriv->ai_n_chan = cmd->chanlist_len;
	devpriv->ai_n_scanlen = cmd->scan_end_arg;
	devpriv->ai_chanlist = cmd->chanlist;
	devpriv->ai_data = s->async->prealloc_buf;
	devpriv->ai_data_len = s->async->prealloc_bufsz;
	devpriv->ai_timer1 = 0;
	devpriv->ai_timer2 = 0;
	devpriv->ai_add_front = 0;
	devpriv->ai_add_back = 0;
	devpriv->ai_maskerr = 0x10e;

	
	if (cmd->start_src == TRIG_EXT)
		devpriv->ai12_startstop |= START_AI_EXT;
	if (cmd->stop_src == TRIG_EXT) {
		devpriv->ai_neverending = 1;
		devpriv->ai12_startstop |= STOP_AI_EXT;
	}
	if (cmd->start_src == TRIG_INT) {
		devpriv->ai12_startstop |= START_AI_INT;
		devpriv->ai_inttrig_start = cmd->start_arg;
		s->async->inttrig = pci9118_ai_inttrig;
	}
#if 0
	if (cmd->stop_src == TRIG_INT) {
		devpriv->ai_neverending = 1;
		devpriv->ai12_startstop |= STOP_AI_INT;
	}
#endif
	if (cmd->stop_src == TRIG_NONE)
		devpriv->ai_neverending = 1;
	if (cmd->stop_src == TRIG_COUNT) {
		devpriv->ai_scans = cmd->stop_arg;
		devpriv->ai_neverending = 0;
	} else {
		devpriv->ai_scans = 0;
	}

	
	if (cmd->convert_src == TRIG_NOW)
		devpriv->usessh = 1;
	
	else
		devpriv->usessh = 0;
				

	DPRINTK("1 neverending=%d scans=%u usessh=%d ai_startstop=0x%2x\n",
		devpriv->ai_neverending, devpriv->ai_scans, devpriv->usessh,
		devpriv->ai12_startstop);

	devpriv->ai_add_front = 0;
	devpriv->ai_add_back = 0;
	devpriv->useeoshandle = 0;
	if (devpriv->master) {
		devpriv->usedma = 1;
		if ((cmd->flags & TRIG_WAKE_EOS) &&
		    (devpriv->ai_n_scanlen == 1)) {
			if (cmd->convert_src == TRIG_NOW)
				devpriv->ai_add_back = 1;
			if (cmd->convert_src == TRIG_TIMER) {
				devpriv->usedma = 0;
			}
		}
		if ((cmd->flags & TRIG_WAKE_EOS) &&
		    (devpriv->ai_n_scanlen & 1) &&
		    (devpriv->ai_n_scanlen > 1)) {
			if (cmd->scan_begin_src == TRIG_FOLLOW) {
				devpriv->usedma = 0;
			} else {	
				devpriv->ai_add_back = 1;
			}
		}
	} else {	
		devpriv->usedma = 0;
	}

	if (devpriv->usessh && devpriv->softsshdelay) {
		devpriv->ai_add_front = 2;
		if ((devpriv->usedma == 1) && (devpriv->ai_add_back == 1)) {
							
			devpriv->ai_add_front++;
			devpriv->ai_add_back = 0;
		}
		if (cmd->convert_arg < this_board->ai_ns_min)
			cmd->convert_arg = this_board->ai_ns_min;
		addchans = devpriv->softsshdelay / cmd->convert_arg;
		if (devpriv->softsshdelay % cmd->convert_arg)
			addchans++;
		if (addchans > (devpriv->ai_add_front - 1)) {
							
			devpriv->ai_add_front = addchans + 1;
			if (devpriv->usedma == 1)
				if ((devpriv->ai_add_front +
				     devpriv->ai_n_chan +
				     devpriv->ai_add_back) & 1)
					devpriv->ai_add_front++;
							
		}
	}
	
	devpriv->ai_n_realscanlen =	
	    (devpriv->ai_add_front + devpriv->ai_n_chan +
	     devpriv->ai_add_back) * (devpriv->ai_n_scanlen /
				      devpriv->ai_n_chan);

	DPRINTK("2 usedma=%d realscan=%d af=%u n_chan=%d ab=%d n_scanlen=%d\n",
		devpriv->usedma,
		devpriv->ai_n_realscanlen, devpriv->ai_add_front,
		devpriv->ai_n_chan, devpriv->ai_add_back,
		devpriv->ai_n_scanlen);

	
	if (!check_channel_list(dev, s, devpriv->ai_n_chan,
				devpriv->ai_chanlist, devpriv->ai_add_front,
				devpriv->ai_add_back))
		return -EINVAL;
	if (!setup_channel_list(dev, s, devpriv->ai_n_chan,
				devpriv->ai_chanlist, 0, devpriv->ai_add_front,
				devpriv->ai_add_back, devpriv->usedma,
				devpriv->useeoshandle))
		return -EINVAL;

	
	if (((cmd->scan_begin_src == TRIG_FOLLOW) ||
		(cmd->scan_begin_src == TRIG_EXT) ||
		(cmd->scan_begin_src == TRIG_INT)) &&
		(cmd->convert_src == TRIG_TIMER)) {
					
		if (cmd->scan_begin_src == TRIG_EXT)
			devpriv->ai_do = 4;
		else
			devpriv->ai_do = 1;
		pci9118_calc_divisors(devpriv->ai_do, dev, s,
				      &cmd->scan_begin_arg, &cmd->convert_arg,
				      devpriv->ai_flags,
				      devpriv->ai_n_realscanlen,
				      &devpriv->ai_divisor1,
				      &devpriv->ai_divisor2, devpriv->usessh,
				      devpriv->ai_add_front);
		devpriv->ai_timer2 = cmd->convert_arg;
	}

	if ((cmd->scan_begin_src == TRIG_TIMER) &&
		((cmd->convert_src == TRIG_TIMER) ||
		(cmd->convert_src == TRIG_NOW))) {
						
		if (!devpriv->usedma) {
			comedi_error(dev,
				     "cmd->scan_begin_src=TRIG_TIMER works "
						"only with bus mastering!");
			return -EIO;
		}

		devpriv->ai_do = 2;
		pci9118_calc_divisors(devpriv->ai_do, dev, s,
				      &cmd->scan_begin_arg, &cmd->convert_arg,
				      devpriv->ai_flags,
				      devpriv->ai_n_realscanlen,
				      &devpriv->ai_divisor1,
				      &devpriv->ai_divisor2, devpriv->usessh,
				      devpriv->ai_add_front);
		devpriv->ai_timer1 = cmd->scan_begin_arg;
		devpriv->ai_timer2 = cmd->convert_arg;
	}

	if ((cmd->scan_begin_src == TRIG_FOLLOW)
	    && (cmd->convert_src == TRIG_EXT)) {
		devpriv->ai_do = 3;
	}

	start_pacer(dev, -1, 0, 0);	

	devpriv->AdControlReg = 0;	
	outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);
	devpriv->AdFunctionReg = AdFunction_PDTrg | AdFunction_PETrg;
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
	udelay(1);
	outl(0, dev->iobase + PCI9118_DELFIFO);	
	inl(dev->iobase + PCI9118_ADSTAT);	
	inl(dev->iobase + PCI9118_INTSRC);

	devpriv->ai_act_scan = 0;
	devpriv->ai_act_dmapos = 0;
	s->async->cur_chan = 0;
	devpriv->ai_buf_ptr = 0;

	if (devpriv->usedma)
		ret = pci9118_ai_docmd_dma(dev, s);
	else
		ret = pci9118_ai_docmd_sampl(dev, s);

	DPRINTK("adl_pci9118 EDBG: END: pci9118_ai_cmd()\n");
	return ret;
}

static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s, int n_chan,
			      unsigned int *chanlist, int frontadd, int backadd)
{
	unsigned int i, differencial = 0, bipolar = 0;

	
	if (n_chan < 1) {
		comedi_error(dev, "range/channel list is empty!");
		return 0;
	}
	if ((frontadd + n_chan + backadd) > s->len_chanlist) {
		printk
		    ("comedi%d: range/channel list is too long for "
						"actual configuration (%d>%d)!",
		     dev->minor, n_chan, s->len_chanlist - frontadd - backadd);
		return 0;
	}

	if (CR_AREF(chanlist[0]) == AREF_DIFF)
		differencial = 1;	
	if (CR_RANGE(chanlist[0]) < PCI9118_BIPOLAR_RANGES)
		bipolar = 1;	
	if (n_chan > 1)
		for (i = 1; i < n_chan; i++) {	
			if ((CR_AREF(chanlist[i]) == AREF_DIFF) !=
			    (differencial)) {
				comedi_error(dev,
					     "Differencial and single ended "
						"inputs can't be mixtured!");
				return 0;
			}
			if ((CR_RANGE(chanlist[i]) < PCI9118_BIPOLAR_RANGES) !=
			    (bipolar)) {
				comedi_error(dev,
					     "Bipolar and unipolar ranges "
							"can't be mixtured!");
				return 0;
			}
			if ((!devpriv->usemux) & (differencial) &
			    (CR_CHAN(chanlist[i]) >= this_board->n_aichand)) {
				comedi_error(dev,
					     "If AREF_DIFF is used then is "
					"available only first 8 channels!");
				return 0;
			}
		}

	return 1;
}

static int setup_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s, int n_chan,
			      unsigned int *chanlist, int rot, int frontadd,
			      int backadd, int usedma, char useeos)
{
	unsigned int i, differencial = 0, bipolar = 0;
	unsigned int scanquad, gain, ssh = 0x00;

	DPRINTK
	    ("adl_pci9118 EDBG: BGN: setup_channel_list"
						"(%d,.,%d,.,%d,%d,%d,%d)\n",
	     dev->minor, n_chan, rot, frontadd, backadd, usedma);

	if (usedma == 1) {
		rot = 8;
		usedma = 0;
	}

	if (CR_AREF(chanlist[0]) == AREF_DIFF)
		differencial = 1;	
	if (CR_RANGE(chanlist[0]) < PCI9118_BIPOLAR_RANGES)
		bipolar = 1;	

	

	if (!bipolar) {
		devpriv->AdControlReg |= AdControl_UniP;
							
	} else {
		devpriv->AdControlReg &= ((~AdControl_UniP) & 0xff);
							
	}

	if (differencial) {
		devpriv->AdControlReg |= AdControl_Diff;
							
	} else {
		devpriv->AdControlReg &= ((~AdControl_Diff) & 0xff);
						
	}

	outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);
								

	outl(2, dev->iobase + PCI9118_SCANMOD);
					
	outl(0, dev->iobase + PCI9118_SCANMOD);
	outl(1, dev->iobase + PCI9118_SCANMOD);

#ifdef PCI9118_PARANOIDCHECK
	devpriv->chanlistlen = n_chan;
	for (i = 0; i < (PCI9118_CHANLEN + 1); i++)
		devpriv->chanlist[i] = 0x55aa;
#endif

	if (frontadd) {		
		ssh = devpriv->softsshsample;
		DPRINTK("FA: %04x: ", ssh);
		for (i = 0; i < frontadd; i++) {
						
			scanquad = CR_CHAN(chanlist[0]);
						
			gain = CR_RANGE(chanlist[0]);
						
			scanquad |= ((gain & 0x03) << 8);
			outl(scanquad | ssh, dev->iobase + PCI9118_GAIN);
			DPRINTK("%02x ", scanquad | ssh);
			ssh = devpriv->softsshhold;
		}
		DPRINTK("\n ");
	}

	DPRINTK("SL: ", ssh);
	for (i = 0; i < n_chan; i++) {	
		scanquad = CR_CHAN(chanlist[i]);	
#ifdef PCI9118_PARANOIDCHECK
		devpriv->chanlist[i ^ usedma] = (scanquad & 0xf) << rot;
#endif
		gain = CR_RANGE(chanlist[i]);		
		scanquad |= ((gain & 0x03) << 8);
		outl(scanquad | ssh, dev->iobase + PCI9118_GAIN);
		DPRINTK("%02x ", scanquad | ssh);
	}
	DPRINTK("\n ");

	if (backadd) {		
		DPRINTK("BA: %04x: ", ssh);
		for (i = 0; i < backadd; i++) {	
			scanquad = CR_CHAN(chanlist[0]);
							
			gain = CR_RANGE(chanlist[0]);	
			scanquad |= ((gain & 0x03) << 8);
			outl(scanquad | ssh, dev->iobase + PCI9118_GAIN);
			DPRINTK("%02x ", scanquad | ssh);
		}
		DPRINTK("\n ");
	}
#ifdef PCI9118_PARANOIDCHECK
	devpriv->chanlist[n_chan ^ usedma] = devpriv->chanlist[0 ^ usedma];
						
	if (useeos) {
		for (i = 1; i < n_chan; i++) {	
			devpriv->chanlist[(n_chan + i) ^ usedma] =
			    (CR_CHAN(chanlist[i]) & 0xf) << rot;
		}
		devpriv->chanlist[(2 * n_chan) ^ usedma] =
						devpriv->chanlist[0 ^ usedma];
						
		useeos = 2;
	} else {
		useeos = 1;
	}
#ifdef PCI9118_EXTDEBUG
	DPRINTK("CHL: ");
	for (i = 0; i <= (useeos * n_chan); i++)
		DPRINTK("%04x ", devpriv->chanlist[i]);

	DPRINTK("\n ");
#endif
#endif
	outl(0, dev->iobase + PCI9118_SCANMOD);	
	

	DPRINTK("adl_pci9118 EDBG: END: setup_channel_list()\n");
	return 1;		
}

static void pci9118_calc_divisors(char mode, struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int *tim1, unsigned int *tim2,
				  unsigned int flags, int chans,
				  unsigned int *div1, unsigned int *div2,
				  char usessh, unsigned int chnsshfront)
{
	DPRINTK
	    ("adl_pci9118 EDBG: BGN: pci9118_calc_divisors"
					"(%d,%d,.,%u,%u,%u,%d,.,.,,%u,%u)\n",
	     mode, dev->minor, *tim1, *tim2, flags, chans, usessh, chnsshfront);
	switch (mode) {
	case 1:
	case 4:
		if (*tim2 < this_board->ai_ns_min)
			*tim2 = this_board->ai_ns_min;
		i8253_cascade_ns_to_timer(devpriv->i8254_osc_base, div1, div2,
					  tim2, flags & TRIG_ROUND_NEAREST);
		DPRINTK("OSC base=%u div1=%u div2=%u timer1=%u\n",
			devpriv->i8254_osc_base, *div1, *div2, *tim1);
		break;
	case 2:
		if (*tim2 < this_board->ai_ns_min)
			*tim2 = this_board->ai_ns_min;
		DPRINTK("1 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);
		*div1 = *tim2 / devpriv->i8254_osc_base;
						
		DPRINTK("2 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);
		if (*div1 < this_board->ai_pacer_min)
			*div1 = this_board->ai_pacer_min;
		DPRINTK("3 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);
		*div2 = *tim1 / devpriv->i8254_osc_base;	
		DPRINTK("4 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);
		*div2 = *div2 / *div1;		
		DPRINTK("5 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);
		if (*div2 < chans)
			*div2 = chans;
		DPRINTK("6 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);

		*tim2 = *div1 * devpriv->i8254_osc_base;
							

		if (usessh & (chnsshfront == 0))	
			if (*div2 < (chans + 2))
				*div2 = chans + 2;

		DPRINTK("7 div1=%u div2=%u timer1=%u timer2=%u\n", *div1, *div2,
			*tim1, *tim2);
		*tim1 = *div1 * *div2 * devpriv->i8254_osc_base;
		DPRINTK("OSC base=%u div1=%u div2=%u timer1=%u timer2=%u\n",
			devpriv->i8254_osc_base, *div1, *div2, *tim1, *tim2);
		break;
	}
	DPRINTK("adl_pci9118 EDBG: END: pci9118_calc_divisors(%u,%u)\n",
		*div1, *div2);
}

static void start_pacer(struct comedi_device *dev, int mode,
			unsigned int divisor1, unsigned int divisor2)
{
	outl(0x74, dev->iobase + PCI9118_CNTCTRL);
	outl(0xb4, dev->iobase + PCI9118_CNTCTRL);
	udelay(1);

	if ((mode == 1) || (mode == 2) || (mode == 4)) {
		outl(divisor2 & 0xff, dev->iobase + PCI9118_CNT2);
		outl((divisor2 >> 8) & 0xff, dev->iobase + PCI9118_CNT2);
		outl(divisor1 & 0xff, dev->iobase + PCI9118_CNT1);
		outl((divisor1 >> 8) & 0xff, dev->iobase + PCI9118_CNT1);
	}
}

static int pci9118_exttrg_add(struct comedi_device *dev, unsigned char source)
{
	if (source > 3)
		return -1;				
	devpriv->exttrg_users |= (1 << source);
	devpriv->IntControlReg |= Int_DTrg;
	outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
	outl(inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR) | 0x1f00,
					devpriv->iobase_a + AMCC_OP_REG_INTCSR);
							
	return 0;
}

static int pci9118_exttrg_del(struct comedi_device *dev, unsigned char source)
{
	if (source > 3)
		return -1;			
	devpriv->exttrg_users &= ~(1 << source);
	if (!devpriv->exttrg_users) {	
		devpriv->IntControlReg &= ~Int_DTrg;
		if (!devpriv->IntControlReg)	
			outl(inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR) &
					(~0x00001f00),
					devpriv->iobase_a + AMCC_OP_REG_INTCSR);
						
		outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
	}
	return 0;
}

static int pci9118_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	if (devpriv->usedma)
		outl(inl(devpriv->iobase_a + AMCC_OP_REG_MCSR) &
			(~EN_A2P_TRANSFERS),
			devpriv->iobase_a + AMCC_OP_REG_MCSR);	
	pci9118_exttrg_del(dev, EXTTRG_AI);
	start_pacer(dev, 0, 0, 0);	
	devpriv->AdFunctionReg = AdFunction_PDTrg | AdFunction_PETrg;
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);
	devpriv->AdControlReg = 0x00;
	outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);
	outl(0, dev->iobase + PCI9118_BURST);
	outl(1, dev->iobase + PCI9118_SCANMOD);
	outl(2, dev->iobase + PCI9118_SCANMOD);	
	outl(0, dev->iobase + PCI9118_DELFIFO);	

	devpriv->ai_do = 0;
	devpriv->usedma = 0;

	devpriv->ai_act_scan = 0;
	devpriv->ai_act_dmapos = 0;
	s->async->cur_chan = 0;
	s->async->inttrig = NULL;
	devpriv->ai_buf_ptr = 0;
	devpriv->ai_neverending = 0;
	devpriv->dma_actbuf = 0;

	if (!devpriv->IntControlReg)
		outl(inl(devpriv->iobase_a + AMCC_OP_REG_INTCSR) | 0x1f00,
					devpriv->iobase_a + AMCC_OP_REG_INTCSR);
							

	return 0;
}

static int pci9118_reset(struct comedi_device *dev)
{
	devpriv->IntControlReg = 0;
	devpriv->exttrg_users = 0;
	inl(dev->iobase + PCI9118_INTCTRL);
	outl(devpriv->IntControlReg, dev->iobase + PCI9118_INTCTRL);
						
	outl(0x30, dev->iobase + PCI9118_CNTCTRL);
	start_pacer(dev, 0, 0, 0);		
	devpriv->AdControlReg = 0;
	outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);
	outl(0, dev->iobase + PCI9118_BURST);
	outl(1, dev->iobase + PCI9118_SCANMOD);
	outl(2, dev->iobase + PCI9118_SCANMOD);	
	devpriv->AdFunctionReg = AdFunction_PDTrg | AdFunction_PETrg;
	outl(devpriv->AdFunctionReg, dev->iobase + PCI9118_ADFUNC);

	devpriv->ao_data[0] = 2047;
	devpriv->ao_data[1] = 2047;
	outl(devpriv->ao_data[0], dev->iobase + PCI9118_DA1);
						
	outl(devpriv->ao_data[1], dev->iobase + PCI9118_DA2);
	outl(0, dev->iobase + PCI9118_DO);	
	udelay(10);
	inl(dev->iobase + PCI9118_AD_DATA);
	outl(0, dev->iobase + PCI9118_DELFIFO);	
	outl(0, dev->iobase + PCI9118_INTSRC);	
	inl(dev->iobase + PCI9118_ADSTAT);	
	inl(dev->iobase + PCI9118_INTSRC);	
	devpriv->AdControlReg = 0;
	outl(devpriv->AdControlReg, dev->iobase + PCI9118_ADCNTRL);

	devpriv->cnt0_users = 0;
	devpriv->exttrg_users = 0;

	return 0;
}

static int pci9118_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret, pages, i;
	unsigned short master;
	unsigned int irq;
	unsigned long iobase_a, iobase_9;
	struct pci_dev *pcidev;
	int opt_bus, opt_slot;
	const char *errstr;
	unsigned char pci_bus, pci_slot, pci_func;
	u16 u16w;

	printk("comedi%d: adl_pci9118: board=%s", dev->minor, this_board->name);

	opt_bus = it->options[0];
	opt_slot = it->options[1];
	if (it->options[3] & 1)
		master = 0;	
	else
		master = 1;

	ret = alloc_private(dev, sizeof(struct pci9118_private));
	if (ret < 0) {
		printk(" - Allocation failed!\n");
		return -ENOMEM;
	}

	
	errstr = "not found!";
	pcidev = NULL;
	while (NULL != (pcidev = pci_get_device(PCI_VENDOR_ID_AMCC,
						this_board->device_id,
						pcidev))) {
		
		if (opt_bus || opt_slot) {
			
			if (opt_bus != pcidev->bus->number
			    || opt_slot != PCI_SLOT(pcidev->devfn))
				continue;	
		}
		if (comedi_pci_enable(pcidev, "adl_pci9118")) {
			errstr =
			    "failed to enable PCI device and request regions!";
			continue;
		}
		break;
	}

	if (!pcidev) {
		if (opt_bus || opt_slot) {
			printk(KERN_ERR " - Card at b:s %d:%d %s\n",
			       opt_bus, opt_slot, errstr);
		} else {
			printk(KERN_ERR " - Card %s\n", errstr);
		}
		return -EIO;
	}

	if (master)
		pci_set_master(pcidev);


	pci_bus = pcidev->bus->number;
	pci_slot = PCI_SLOT(pcidev->devfn);
	pci_func = PCI_FUNC(pcidev->devfn);
	irq = pcidev->irq;
	iobase_a = pci_resource_start(pcidev, 0);
	iobase_9 = pci_resource_start(pcidev, 2);

	printk(KERN_ERR ", b:s:f=%d:%d:%d, io=0x%4lx, 0x%4lx", pci_bus,
				pci_slot, pci_func, iobase_9, iobase_a);

	dev->iobase = iobase_9;
	dev->board_name = this_board->name;

	devpriv->pcidev = pcidev;
	devpriv->iobase_a = iobase_a;

	pci9118_reset(dev);

	if (it->options[3] & 2)
		irq = 0;	
	if (irq > 0) {
		if (request_irq(irq, interrupt_pci9118, IRQF_SHARED,
				"ADLink PCI-9118", dev)) {
			printk(", unable to allocate IRQ %d, DISABLING IT",
			       irq);
			irq = 0;	
		} else {
			printk(", irq=%u", irq);
		}
	} else {
		printk(", IRQ disabled");
	}

	dev->irq = irq;

	if (master) {		
		devpriv->dma_doublebuf = 0;
		for (i = 0; i < 2; i++) {
			for (pages = 4; pages >= 0; pages--) {
				devpriv->dmabuf_virt[i] =
				    (short *)__get_free_pages(GFP_KERNEL,
							      pages);
				if (devpriv->dmabuf_virt[i])
					break;
			}
			if (devpriv->dmabuf_virt[i]) {
				devpriv->dmabuf_pages[i] = pages;
				devpriv->dmabuf_size[i] = PAGE_SIZE * pages;
				devpriv->dmabuf_samples[i] =
				    devpriv->dmabuf_size[i] >> 1;
				devpriv->dmabuf_hw[i] =
				    virt_to_bus((void *)
						devpriv->dmabuf_virt[i]);
			}
		}
		if (!devpriv->dmabuf_virt[0]) {
			printk(", Can't allocate DMA buffer, DMA disabled!");
			master = 0;
		}

		if (devpriv->dmabuf_virt[1])
			devpriv->dma_doublebuf = 1;

	}

	devpriv->master = master;
	if (devpriv->master)
		printk(", bus master");
	else
		printk(", no bus master");

	devpriv->usemux = 0;
	if (it->options[2] > 0) {
		devpriv->usemux = it->options[2];
		if (devpriv->usemux > 256)
			devpriv->usemux = 256;	
		if (it->options[4] > 0)
			if (devpriv->usemux > 128) {
				devpriv->usemux = 128;
					
			}
		printk(", ext. mux %d channels", devpriv->usemux);
	}

	devpriv->softsshdelay = it->options[4];
	if (devpriv->softsshdelay < 0) {
					
		devpriv->softsshdelay = -devpriv->softsshdelay;
		devpriv->softsshsample = 0x80;
		devpriv->softsshhold = 0x00;
	} else {
		devpriv->softsshsample = 0x00;
		devpriv->softsshhold = 0x80;
	}

	printk(".\n");

	pci_read_config_word(devpriv->pcidev, PCI_COMMAND, &u16w);
	pci_write_config_word(devpriv->pcidev, PCI_COMMAND, u16w | 64);
				

	ret = alloc_subdevices(dev, 4);
	if (ret < 0)
		return ret;

	s = dev->subdevices + 0;
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_COMMON | SDF_GROUND | SDF_DIFF;
	if (devpriv->usemux)
		s->n_chan = devpriv->usemux;
	else
		s->n_chan = this_board->n_aichan;

	s->maxdata = this_board->ai_maxdata;
	s->len_chanlist = this_board->n_aichanlist;
	s->range_table = this_board->rangelist_ai;
	s->cancel = pci9118_ai_cancel;
	s->insn_read = pci9118_insn_read_ai;
	if (dev->irq) {
		s->subdev_flags |= SDF_CMD_READ;
		s->do_cmdtest = pci9118_ai_cmdtest;
		s->do_cmd = pci9118_ai_cmd;
		s->munge = pci9118_ai_munge;
	}

	s = dev->subdevices + 1;
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = this_board->n_aochan;
	s->maxdata = this_board->ao_maxdata;
	s->len_chanlist = this_board->n_aochan;
	s->range_table = this_board->rangelist_ao;
	s->insn_write = pci9118_insn_write_ao;
	s->insn_read = pci9118_insn_read_ao;

	s = dev->subdevices + 2;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 1;
	s->len_chanlist = 4;
	s->range_table = &range_digital;
	s->io_bits = 0;		
	s->insn_bits = pci9118_insn_bits_di;

	s = dev->subdevices + 3;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 1;
	s->len_chanlist = 4;
	s->range_table = &range_digital;
	s->io_bits = 0xf;	
	s->insn_bits = pci9118_insn_bits_do;

	devpriv->valid = 1;
	devpriv->i8254_osc_base = 250;	
	devpriv->ai_maskharderr = 0x10a;
					
	if (it->options[5])		
		devpriv->ai_maskharderr &= ~it->options[5];

	switch (this_board->ai_maxdata) {
	case 0xffff:
		devpriv->ai16bits = 1;
		break;
	default:
		devpriv->ai16bits = 0;
		break;
	}
	return 0;
}

static int pci9118_detach(struct comedi_device *dev)
{
	if (dev->private) {
		if (devpriv->valid)
			pci9118_reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if (devpriv->pcidev) {
			if (dev->iobase)
				comedi_pci_disable(devpriv->pcidev);

			pci_dev_put(devpriv->pcidev);
		}
		if (devpriv->dmabuf_virt[0])
			free_pages((unsigned long)devpriv->dmabuf_virt[0],
				   devpriv->dmabuf_pages[0]);
		if (devpriv->dmabuf_virt[1])
			free_pages((unsigned long)devpriv->dmabuf_virt[1],
				   devpriv->dmabuf_pages[1]);
	}

	return 0;
}


MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");