/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef DRM_GEM_VRAM_HELPER_H
#define DRM_GEM_VRAM_HELPER_H

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

#include <linux/kernel.h> /* for container_of() */

struct drm_mode_create_dumb;
struct drm_vram_mm_funcs;
struct filp;
struct vm_area_struct;

#define DRM_GEM_VRAM_PL_FLAG_VRAM	TTM_PL_FLAG_VRAM
#define DRM_GEM_VRAM_PL_FLAG_SYSTEM	TTM_PL_FLAG_SYSTEM
#define DRM_GEM_VRAM_PL_FLAG_TOPDOWN	TTM_PL_FLAG_TOPDOWN

/*
 * Buffer-object helpers
 */

/**
 * struct drm_gem_vram_object - GEM object backed by VRAM
 * @gem:	GEM object
 * @bo:		TTM buffer object
 * @kmap:	Mapping information for @bo
 * @placement:	TTM placement information. Supported placements are \
	%TTM_PL_VRAM and %TTM_PL_SYSTEM
 * @placements:	TTM placement information.
 * @pin_count:	Pin counter
 *
 * The type struct drm_gem_vram_object represents a GEM object that is
 * backed by VRAM. It can be used for simple framebuffer devices with
 * dedicated memory. The buffer object can be evicted to system memory if
 * video memory becomes scarce.
 *
 * GEM VRAM objects perform reference counting for pin and mapping
 * operations. So a buffer object that has been pinned N times with
 * drm_gem_vram_pin() must be unpinned N times with
 * drm_gem_vram_unpin(). The same applies to pairs of
 * drm_gem_vram_kmap() and drm_gem_vram_kunmap(), as well as pairs of
 * drm_gem_vram_vmap() and drm_gem_vram_vunmap().
 */
struct drm_gem_vram_object {
	struct ttm_buffer_object bo;
	struct ttm_bo_kmap_obj kmap;

	/**
	 * @kmap_use_count:
	 *
	 * Reference count on the virtual address.
	 * The address are un-mapped when the count reaches zero.
	 */
	unsigned int kmap_use_count;

	/* Supported placements are %TTM_PL_VRAM and %TTM_PL_SYSTEM */
	struct ttm_placement placement;
	struct ttm_place placements[2];

	int pin_count;
};

/**
 * Returns the container of type &struct drm_gem_vram_object
 * for field bo.
 * @bo:		the VRAM buffer object
 * Returns:	The containing GEM VRAM object
 */
static inline struct drm_gem_vram_object *drm_gem_vram_of_bo(
	struct ttm_buffer_object *bo)
{
	return container_of(bo, struct drm_gem_vram_object, bo);
}

/**
 * Returns the container of type &struct drm_gem_vram_object
 * for field gem.
 * @gem:	the GEM object
 * Returns:	The containing GEM VRAM object
 */
static inline struct drm_gem_vram_object *drm_gem_vram_of_gem(
	struct drm_gem_object *gem)
{
	return container_of(gem, struct drm_gem_vram_object, bo.base);
}

struct drm_gem_vram_object *drm_gem_vram_create(struct drm_device *dev,
						struct ttm_bo_device *bdev,
						size_t size,
						unsigned long pg_align,
						bool interruptible);
void drm_gem_vram_put(struct drm_gem_vram_object *gbo);
u64 drm_gem_vram_mmap_offset(struct drm_gem_vram_object *gbo);
s64 drm_gem_vram_offset(struct drm_gem_vram_object *gbo);
int drm_gem_vram_pin(struct drm_gem_vram_object *gbo, unsigned long pl_flag);
int drm_gem_vram_unpin(struct drm_gem_vram_object *gbo);
void *drm_gem_vram_kmap(struct drm_gem_vram_object *gbo, bool map,
			bool *is_iomem);
void drm_gem_vram_kunmap(struct drm_gem_vram_object *gbo);
void *drm_gem_vram_vmap(struct drm_gem_vram_object *gbo);
void drm_gem_vram_vunmap(struct drm_gem_vram_object *gbo, void *vaddr);

int drm_gem_vram_fill_create_dumb(struct drm_file *file,
				  struct drm_device *dev,
				  struct ttm_bo_device *bdev,
				  unsigned long pg_align,
				  bool interruptible,
				  struct drm_mode_create_dumb *args);

/*
 * Helpers for struct drm_driver
 */

int drm_gem_vram_driver_dumb_create(struct drm_file *file,
				    struct drm_device *dev,
				    struct drm_mode_create_dumb *args);
int drm_gem_vram_driver_dumb_mmap_offset(struct drm_file *file,
					 struct drm_device *dev,
					 uint32_t handle, uint64_t *offset);

/**
 * define DRM_GEM_VRAM_DRIVER - default callback functions for \
	&struct drm_driver
 *
 * Drivers that use VRAM MM and GEM VRAM can use this macro to initialize
 * &struct drm_driver with default functions.
 */
#define DRM_GEM_VRAM_DRIVER \
	.debugfs_init             = drm_vram_mm_debugfs_init, \
	.dumb_create		  = drm_gem_vram_driver_dumb_create, \
	.dumb_map_offset	  = drm_gem_vram_driver_dumb_mmap_offset, \
	.gem_prime_mmap		  = drm_gem_prime_mmap

/*
 *  VRAM memory manager
 */

/**
 * struct drm_vram_mm - An instance of VRAM MM
 * @vram_base:	Base address of the managed video memory
 * @vram_size:	Size of the managed video memory in bytes
 * @bdev:	The TTM BO device.
 * @funcs:	TTM BO functions
 *
 * The fields &struct drm_vram_mm.vram_base and
 * &struct drm_vram_mm.vrm_size are managed by VRAM MM, but are
 * available for public read access. Use the field
 * &struct drm_vram_mm.bdev to access the TTM BO device.
 */
struct drm_vram_mm {
	uint64_t vram_base;
	size_t vram_size;

	struct ttm_bo_device bdev;
};

/**
 * drm_vram_mm_of_bdev() - \
	Returns the container of type &struct ttm_bo_device for field bdev.
 * @bdev:	the TTM BO device
 *
 * Returns:
 * The containing instance of &struct drm_vram_mm
 */
static inline struct drm_vram_mm *drm_vram_mm_of_bdev(
	struct ttm_bo_device *bdev)
{
	return container_of(bdev, struct drm_vram_mm, bdev);
}

int drm_vram_mm_debugfs_init(struct drm_minor *minor);

/*
 * Helpers for integration with struct drm_device
 */

struct drm_vram_mm *drm_vram_helper_alloc_mm(
	struct drm_device *dev, uint64_t vram_base, size_t vram_size);
void drm_vram_helper_release_mm(struct drm_device *dev);

/*
 * Helpers for &struct file_operations
 */

int drm_vram_mm_file_operations_mmap(
	struct file *filp, struct vm_area_struct *vma);

/**
 * define DRM_VRAM_MM_FILE_OPERATIONS - default callback functions for \
	&struct file_operations
 *
 * Drivers that use VRAM MM can use this macro to initialize
 * &struct file_operations with default functions.
 */
#define DRM_VRAM_MM_FILE_OPERATIONS \
	.llseek		= no_llseek, \
	.read		= drm_read, \
	.poll		= drm_poll, \
	.unlocked_ioctl = drm_ioctl, \
	.compat_ioctl	= drm_compat_ioctl, \
	.mmap		= drm_vram_mm_file_operations_mmap, \
	.open		= drm_open, \
	.release	= drm_release \


#endif
