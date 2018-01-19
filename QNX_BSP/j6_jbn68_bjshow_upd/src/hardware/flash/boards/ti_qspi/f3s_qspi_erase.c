/*
 * $QNXLicenseC:
 * Copyright 2014, QNX Software Systems.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "f3s_qspi.h"

/*
 * This is the erase callout for QSPI serial NOR flash.
 */
int f3s_qspi_s25fl_erase(f3s_dbase_t *dbase,
					 f3s_access_t *access,
					 uint32_t flags,
					 uint32_t offset)
{
	int	 geo_index;
	int	 size = 0;	

	if (access->service->page(&access->socket, 0, offset, NULL) == NULL)
		return (ERANGE);

	for (geo_index = 0; geo_index < dbase->geo_num; geo_index++) {
		
		if (verbose > 5) {
			fprintf(stderr, "geo_index=%d, unit_pow2=%d, unit_num=%d\n", 
				geo_index,dbase->geo_vect[geo_index].unit_pow2, dbase->geo_vect[geo_index].unit_num);
		}
		
		size += (1 << dbase->geo_vect[geo_index].unit_pow2) * dbase->geo_vect[geo_index].unit_num;

		if (size > offset)
			break;
	}

	if (qspi_flash_s25fl_sector_erase((qspi_dev_t *)access->socket.socket_handle, offset, dbase->geo_vect[geo_index].unit_pow2))
		return (EIO);

	return (EOK);
}

int f3s_qspi_n25q_erase(f3s_dbase_t *dbase,
					 f3s_access_t *access,
					 uint32_t flags,
					 uint32_t offset)
{
	if (access->service->page(&access->socket, 0, offset, NULL) == NULL)
		return (ERANGE);

	if (qspi_flash_n25q_sector_erase((qspi_dev_t *)access->socket.socket_handle, offset))
		return (EIO);

	return (EOK);
}


#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://svn.ott.qnx.com/product/branches/6.6.0/trunk/hardware/flash/boards/ti_qspi/f3s_qspi_erase.c $ $Rev: 744783 $")
#endif
