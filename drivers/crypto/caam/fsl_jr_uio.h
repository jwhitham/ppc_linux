/*
 * CAAM Job RING UIO support header file
 *
 * Copyright 2013 Freescale Semiconductor, Inc
 */

#ifndef FSL_JR_UIO_H
#define FSL_JR_UIO_H

/** UIO command used by user-space driver to request
 *  disabling IRQs on a certain job ring */
#define SEC_UIO_DISABLE_IRQ_CMD         0
/** UIO command used by user-space driver to request
 *  enabling IRQs on a certain job ring */
#define SEC_UIO_ENABLE_IRQ_CMD          1
/** UIO command used by user-space driver to request SEC kernel driver
 *  to simulate that an IRQ is generated on a certain job ring */
#define SEC_UIO_SIMULATE_IRQ_CMD        2

#endif
