/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bootconf.h>
#include <sys/thread.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <vm/seg_kmem.h>
#include <vm/page.h>
#include <vm/as.h>
#include <vm/hat.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/hat_i86.h>
#include <sys/file.h>
#include <sys/kd.h>
#include <sys/sunldi.h>
#include <sys/pnglib.h>
#include <sys/kmem.h>
#include <sys/fbinfo.h>
#include <sys/bootinfo.h>
#include <sys/boot_console.h>

#ifdef __xpv
#include <sys/hypervisor.h>
#endif

/*
 * We select the best fitting splash image from a pool of option. Each
 * splash image is described by the splash_image_desc structure below, which
 * keeps the name of the image and its height. We pick the image whose height
 * is equal or immediately bigger than the framebuffer vertical resolution.
 */
struct splash_image_desc {
	char		*name;
	uint32_t	height;
};

#define	NUM_SPLASH_IMAGES	4
struct splash_image_desc boot_splash_images[NUM_SPLASH_IMAGES] = {
	{"bootbg480.png", 480},
	{"bootbg800.png", 800},
	{"bootbg1050.png", 1050},
	{"bootbg1200.png", 1200}
};

struct splash_image_desc shutdown_splash_images[NUM_SPLASH_IMAGES] = {
	{"shutdownbg480.png", 480},
	{"shutdownbg800.png", 800},
	{"shutdownbg1050.png", 1050},
	{"shutdownbg1200.png", 1200}
};

static int 		graphics_mode;
static kthread_t 	*splash_tid;
static kmutex_t 	pbar_lock;
static kcondvar_t 	pbar_cv;
static uint8_t 		*videomem;
static size_t		videomem_size;
static uint_t		current_step;
static uint8_t		incr;

static uint32_t		graphic_pos_x;
static uint32_t		graphic_pos_y;
static uint32_t		scanline;
static uint32_t		bpp;

/* Number of steps for the boot spinner animation. */
#define	BOOT_ANIM_STEPS		15
#define	SHUTDOWN_ANIM_STEPS	20
#define	MAX_ANIM_STEPS		SHUTDOWN_ANIM_STEPS

static uint8_t		*animation_loop[MAX_ANIM_STEPS];
static int		animation_x;
static int		animation_y;
static int		actual_steps;
static uint32_t		animation_size;
static boolean_t	animation_done;

#define	BOOT_PHASE	(1)
#define	SHUTDOWN_PHASE	(2)

static int		current_phase;

/*
 * Track if something waiting for input from the console needs to reclaim
 * it back from the graphic state.
 */
boolean_t		check_reset_console = B_FALSE;

#define	IMG_TO_SCREEN	(1)
#define	SCREEN_TO_IMG	(2)

#define	copy_to_screen(data)	copy_screen(data, IMG_TO_SCREEN)
#define	copy_from_screen(data)	copy_screen(data, SCREEN_TO_IMG)

/*
 * The images we use at boot time are saved into this path both on the
 * filesystem and in the boot archive.
 */
#define	BOOT_IMAGES_PATH	"/boot/solaris/boot-images"
#define	BOOT_ANIM_PREFIX	"spinner"

/*
 * Images we use for the shutdown graphic don't need to be in the boot archive,
 * so we store them in a separate path.
 */
#define	SHUTDOWN_IMAGES_PATH	"/boot/solaris/images"
#define	SHUTDOWN_ANIM_PREFIX	"bar"

/* Local static functions. */
#if !defined(__xpv)

/*
 * Map the framebuffer physical range in memory.
 */
static unsigned long
map_video_range(uint64_t start, size_t *size)
{
	uint_t		pgoffset;
	uint64_t 	base;
	pgcnt_t 	npages;
	caddr_t 	cvaddr;
	int 		hat_flags = HAT_LOAD_LOCK;
	uint_t 		hat_attr = HAT_MERGING_OK | HAT_PLAT_NOCACHE;
	pfn_t 		pfn;

	if (size == 0)
		return (0);

	pgoffset = start & PAGEOFFSET;
	base = start - pgoffset;
	npages = btopr(*size + pgoffset);
	cvaddr = vmem_alloc(heap_arena, ptob(npages), VM_NOSLEEP);
	if (cvaddr == NULL)
		return (NULL);

	pfn = btop(base);

	hat_devload(kas.a_hat, cvaddr, ptob(npages), pfn,
	    PROT_READ|PROT_WRITE|hat_attr, hat_flags);

	/* Record the exact size to pass to hat_unload later on. */
	*size = ptob(npages);
	return (unsigned long)(cvaddr + pgoffset);
}

static void
unmap_video_range()
{
	if (videomem == NULL)
		return;

	hat_unload(kas.a_hat, (char *)videomem, videomem_size,
	    HAT_UNLOAD_UNLOCK);
	vmem_free(heap_arena, videomem, videomem_size);
	videomem = NULL;
}

/*
 * Generic function to copy to and from the screen.
 * what is either IMG_TO_SCREEN or SCREEN_TO_IMG.
 * This function is actually not that generic, in fact it only copies
 * to/from a (progress-image-sized) chunk of the screen at the position where
 * the progress image is supposed to be,
 * Both the image size and the position have been computed by splash_init().
 */
static void
copy_screen(uint8_t *data, uint8_t what)
{
	uint32_t	offset;
	uint8_t		*start;
	uint32_t	imgsize = animation_x * bpp;
	int		i = 0;

	offset = (graphic_pos_x * bpp)+ graphic_pos_y * scanline;
	start = (uint8_t *)(videomem + offset);

	for (i = 0; i < animation_y; i++) {
		uint8_t	*dest = start + (i * scanline);
		uint8_t	*src = data + (i * imgsize);

		if (what == IMG_TO_SCREEN)
			(void) memcpy(dest, src, imgsize);
		else
			(void) memcpy(src, dest, imgsize);
	}
}

static void
splash_show()
{
	copy_to_screen(animation_loop[current_step]);
}

static void
splash_step()
{
	/* Shutdown animation is a progression bar, not a cycle. */
	if (current_phase == SHUTDOWN_PHASE && animation_done)
		return;

	splash_show();
	current_step++;

	if (current_step == actual_steps) {
		if (current_phase == BOOT_PHASE)
			current_step = 0;
		else
			animation_done = B_TRUE;
	}
}

static void
splash_clear_screen(void)
{
	if (videomem != NULL)
		(void) memset(videomem, 0, videomem_size);
}

static int
load_background_image(char *path, struct splash_image_desc *images)
{
	png_screen_t	screen_info;
	char		image_name[MAXPATHLEN];
	int		i;

	screen_info.width = fb_info.screen_pix.x;
	screen_info.height = fb_info.screen_pix.y;
	screen_info.depth = fb_info.depth;
	screen_info.bpp = fb_info.bpp;
	screen_info.h_pos = PNG_HCENTER;
	screen_info.v_pos = PNG_VCENTER;
	screen_info.rgb = fb_info.rgb;
	screen_info.dstbuf = videomem;

	/* Depending on vertical resolution, pick an appropriate image. */
	for (i = 0; i < NUM_SPLASH_IMAGES; i++) {
		if (images[i].height >= screen_info.height) {
			break;
		}
	}

	/*
	 * Resolution is bigger than the biggest image, just pick the latter
	 * and center it.
	 */
	if (i == NUM_SPLASH_IMAGES)
		i--;

	/*
	 * On high resolutions, the image is smaller than the screen. Clear
	 * any potentially cluttered corner.
	 */
	splash_clear_screen();

	(void) snprintf(image_name, sizeof (image_name), "%s/%s", path,
	    images[i].name);
	if (png_load_file(image_name, &screen_info) != PNG_NO_ERROR) {
		cmn_err(CE_CONT, "error loading %s\n", image_name);
		return (-1);
	}

	return (0);
}

/*
 * Look for the first image of the boot animation cycle and grab width and
 * height from its PNG header. Error out if this fails.
 * Proceed then with loading all the images in the animation. We expect all of
 * them to have the same size and at least one to be present.
 */
static int
load_animation(char *path, char *animation_prefix, int steps)
{
	int		i;
	png_image_t	img_info;
	png_screen_t	screen_info;
	char		image_name[MAXPATHLEN];
	uint32_t	img_size;
	uint8_t		*screen_bg;

	(void) snprintf(image_name, sizeof (image_name), "%s/%s0.png",
	    path, animation_prefix);
	if (png_get_info(image_name, &img_info) != PNG_NO_ERROR) {
		cmn_err(CE_CONT, "unable to load %s\n", image_name);
		return (-1);
	}
	animation_x = img_info.width;
	animation_y = img_info.height;
	img_size = animation_x * animation_y * fb_info.bpp;

	/*
	 * position the spinner 1/2 image height - 10% from the top and
	 * 1/2 image height from the left.
	 */
	graphic_pos_x = (fb_info.screen_pix.x  - animation_x) / 2;
	graphic_pos_y = (fb_info.screen_pix.y / 2) +
	    (fb_info.screen_pix.y * 10 / 100);

	screen_info.width = animation_x;
	screen_info.height = animation_y;
	screen_info.depth = fb_info.depth;
	screen_info.bpp = fb_info.bpp;
	screen_info.h_pos = PNG_HLEFT;
	screen_info.v_pos = PNG_VTOP;
	/* color information. */
	screen_info.rgb = fb_info.rgb;

	/*
	 * save the contents of the screen under the image. We use this as the
	 * background component against which apply the correct alpha
	 * opacity (if one specified).
	 */
	screen_bg = kmem_alloc(img_size, KM_SLEEP);
	copy_from_screen(screen_bg);

	for (i = 0; i < steps; i++) {
		animation_loop[i] = kmem_alloc(img_size, KM_SLEEP);
		(void) memcpy(animation_loop[i], screen_bg, img_size);
		screen_info.dstbuf = animation_loop[i];
		(void) snprintf(image_name, sizeof (image_name),
		    "%s/%s%d.png", path, animation_prefix, i);

		/*
		 * Exit the loop at the first error. This way, users can have
		 * customized animations with less than BOOT_ANIM_STEPS steps.
		 * Note that we are expected to succeed at least once, since
		 * we checked for step0.png above.
		 */
		if (png_load_file(image_name, &screen_info) != PNG_NO_ERROR)
			break;
	}

	/*
	 * Save the number of steps (for the animation loop) and the size of
	 * the spinner image (for later cleanup).
	 */
	actual_steps = i;
	animation_size = img_size;
	animation_done = B_FALSE;
	current_step = 0;

	kmem_free(screen_bg, img_size);
	return (0);
}

/*
 * Initializes the spinning cycle of images.
 * Returns 0 on success, -1 if we do not manage to load the first image of the
 * boot animation or the background image.
 */
static int
splash_init()
{
	char			*base_path;
	char			*anim_prefix;
	int			steps;
	struct splash_image_desc *bg_images;

	if (current_phase == BOOT_PHASE) {
		base_path = BOOT_IMAGES_PATH;
		anim_prefix = BOOT_ANIM_PREFIX;
		steps = BOOT_ANIM_STEPS;
		bg_images = boot_splash_images;
	} else if (current_phase == SHUTDOWN_PHASE) {
		base_path = SHUTDOWN_IMAGES_PATH;
		anim_prefix = SHUTDOWN_ANIM_PREFIX;
		steps = SHUTDOWN_ANIM_STEPS;
		bg_images = shutdown_splash_images;
	} else {
		cmn_err(CE_CONT, "wrong splash phase: %d\n", current_phase);
		return (-1);
	}

	/*
	 * Load background image. For the BOOT phase, whenever GRUB will be
	 * able to load the PNG image for us, this will only be necessary in
	 * the fastreboot case.
	 */
	if (load_background_image(base_path, bg_images) != 0) {
		cmn_err(CE_CONT, "unable to load background splash image\n");
		return (-1);
	}

	if (load_animation(base_path, anim_prefix, steps) != 0) {
		cmn_err(CE_CONT, "unable to load boot animation\n");
		return (-1);
	}

	check_reset_console = B_TRUE;

	/* Display the first image of the boot animation loop. */
	splash_step();
	return (0);
}

/*
 * Free the memory holding the boot animation steps and clear the mapping of
 * the linear framebuffer that we used to paint the screen.
 */
static void
splash_clear_animation()
{
	int	i;

	/* Clear video memory. */
	unmap_video_range();

	/* We need to know the size of the boot animation for kmem_free() */
	if (animation_size == 0)
		return;

	for (i = 0; i < actual_steps; i++) {
		if (animation_loop[i] != NULL) {
			kmem_free(animation_loop[i], animation_size);
			animation_loop[i] = NULL;
		}
	}

	animation_size = 0;
}

/*ARGSUSED*/
static void
splash_thread(void *arg)
{
	clock_t end = drv_usectohz(50000);

	mutex_enter(&pbar_lock);
	while (graphics_mode) {
		splash_step();
		(void) cv_reltimedwait(&pbar_cv, &pbar_lock, end,
		    TR_CLOCK_TICK);
	}
	mutex_exit(&pbar_lock);

	splash_clear_animation();

}

static int
splash_start(int phase)
{
	extern pri_t minclsyspri;

	if (console != CONS_SCREEN_GRAPHICS)
		return (-1);

	if (phase != BOOT_PHASE && phase != SHUTDOWN_PHASE)
		return (-1);

	graphics_mode = 1;
	current_phase = phase;

	/* map video memory to kernel heap */
	videomem_size = fb_info.screen_pix.x * fb_info.screen_pix.y *
	    fb_info.bpp;
	videomem = (uint8_t *)map_video_range(fb_info.phys_addr,
	    &videomem_size);

	if (videomem == NULL) {
		graphics_mode = 0;
		return (-1);
	}
	/* useful variables. */
	bpp = fb_info.bpp;
	scanline = fb_info.screen_pix.x * bpp;

	if (splash_init() == -1) {
		graphics_mode = 0;

		if (phase == BOOT_PHASE)
			splash_clear_screen();

		splash_clear_animation();
		return (-1);
	}

	/*
	 * Start the boot animation thread.
	 */
	if (phase == BOOT_PHASE) {
		splash_tid = thread_create(NULL, 0, splash_thread,
		    NULL, 0, &p0, TS_RUN, minclsyspri);
	}

	return (0);
}

#endif /* !__xpv */

/*
 * Globally exported functions:
 *   splash_boot_start()
 *   splash_shutdown_start()
 *   splash_shutdown_update() (paint one step of the shutdown animation)
 *   splash_stop()
 *   splash_key_abort()
 */
int
splash_boot_start()
{
#if !defined(__xpv)
	return (splash_start(BOOT_PHASE));
#else
	return (0);
#endif
}

int
splash_shutdown_start()
{
#if !defined(__xpv)
	return (splash_start(SHUTDOWN_PHASE));
#else
	return (0);
#endif
}

void
splash_shutdown_update()
{
#if !defined(__xpv)
	if (graphics_mode == 0)
		return;

	splash_step();
#endif
}

void
splash_shutdown_last()
{
#if !defined(__xpv)
	if (graphics_mode == 0)
		return;

	if (animation_done == B_FALSE) {
		current_step = actual_steps - 1;
		splash_step();
	}
#endif
}

void
splash_stop(void)
{
#if !defined(__xpv)
	if (graphics_mode == 0)
		return;
	graphics_mode = 0;

	switch (current_phase) {
	case BOOT_PHASE:
		mutex_enter(&pbar_lock);
		cv_signal(&pbar_cv);
		mutex_exit(&pbar_lock);

		if (splash_tid != NULL)
			thread_join(splash_tid->t_did);
		break;
	case SHUTDOWN_PHASE:
		splash_clear_animation();
		break;
	default:
		break;
	}
#endif
}

/*
 * Stop an happyface animation and reset the console to "text" mode.
 * This function is not safely callable from interrupt context.
 */
void
splash_reset_text_console()
{
#if !defined(__xpv)
	char		*fbpath;
	int		ret;
	ldi_handle_t	hdl;
	ldi_ident_t	li;
	extern char	*consconfig_get_plat_fbpath(void);

	ASSERT(!servicing_interrupt());

	if (graphics_mode == 0)
		return;

	fbpath = consconfig_get_plat_fbpath();
	li = ldi_ident_from_anon();

	if (ldi_open_by_name(fbpath, FWRITE, kcred, &hdl, li) != 0) {
		cmn_err(CE_NOTE, "!ldi_open_by_name failed");
	} else {
		if (ldi_ioctl(hdl, KDSETMODE, KD_RESETTEXT, FKIOCTL,
		    kcred, &ret) != 0)
			cmn_err(CE_NOTE, "!ldi_ioctl for KD_RESETTEXT failed");
		(void) ldi_close(hdl, NULL, kcred);
		check_reset_console = B_FALSE;
	}

	ldi_ident_release(li);
#endif	/* __xpv */
}

/*ARGSUSED*/
void
splash_key_abort(ldi_ident_t li)
{
#if !defined(__xpv)
	splash_reset_text_console();
#endif	/* __xpv */
}
