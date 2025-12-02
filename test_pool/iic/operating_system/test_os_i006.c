/** @file
 * Copyright (c) 2016-2018, 2021, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"

#include "val/include/bsa_acs_gic.h"
#include "val/include/bsa_acs_iic.h"
#include "val/include/bsa_acs_hart.h"
#include "val/include/bsa_acs_memory.h"

#define TEST_NUM   (ACS_GIC_TEST_NUM_BASE + 6)
#define TEST_RULE  "ME_IIC_080_010"
#define TEST_DESC  "Check APLIC controller type in ACPI MADT table"

#define APLIC_DOMAINCFG_MSI 0x04
#define GENMSI_OFFSET 0x3000
#define EEID          0x04  //External Interrupt ID for APLIC
#define TARGET_ADDRESS_OFFSET  0x3004
#define HSTATUS_VGEIN_SHIFT		12
#define HSTATUS_VGEIN			0x0003f000UL

/**
 * @brief Verify the number of supported guest mode interrupt identities in IMSIC
          structure of the ACPI MADT table is at least 63
 */
static
void
payload()
{
  uint64_t aplic_baseaddress, genmsi_address, target_address_i, val_w, val_r;
  uint16_t ex_int_srcs = 0;
  uint32_t value, genmsi_value, vgein;; 
  uint32_t index = val_hart_get_index_mpid(val_hart_get_mpid());

  //STEP 1: Parse ACPI MADT to determine if an APLIC for supervisor interrupt domain is reported
  //STEP 2: If no APLIC is reported then skip the remaining steps.
    if (!val_gic_is_aplic_present()) {
        val_print(ACS_PRINT_ERR, "\n       APLIC controller not found in ACPI MADT", 0);
        val_set_status(index, RESULT_FAIL(TEST_NUM, 1));
        return;
    }

  //Check if APLIC is reported for supervisor interrupt domain
    if (val_gic_get_aplic_address() == 0) {
        val_print(ACS_PRINT_ERR, "\n       APLIC for supervisor interrupt domain is not reported", 0);
        val_set_status(index, RESULT_FAIL(TEST_NUM, 2));
        return;
    }
  //Step 3: Locate the APLIC structure.
  //STEP 4: Verify that number of interrupt delivery control. structures is reported as 0 indicating it is used as a wired-to-MSI bridge
  if(val_gic_get_idc_num() != 0) {
      val_print(ACS_PRINT_ERR, "\n       APLIC IDC number is not zero", 0);
      val_set_status(index, RESULT_FAIL(TEST_NUM, 3));
      return;
  }

  //STEP 5:Verify the domaincfg supports MSI delivery mode and is configured to be in MSI delivery mode.
  aplic_baseaddress = val_gic_get_aplic_address();

  val_print(ACS_PRINT_INFO, "\n       aplic_baseaddress value - 0x%lx", aplic_baseaddress);

  val_memory_map_add_mmio(aplic_baseaddress, 0x4000);

  value = val_mmio_read(aplic_baseaddress);
  if ((value & APLIC_DOMAINCFG_MSI) == 0) {
      val_print(ACS_PRINT_ERR, "\n       APLIC domaincfg does not support MSI delivery mode", 0);
      val_set_status(index, RESULT_FAIL(TEST_NUM, 4));
      return;
  }

  val_print(ACS_PRINT_INFO, "\n       domaincfg value - 0x%lx", value);

  //STEP 6: Write an external interrupt ID to genmsi register and verify that the extempore MSI is delivered to the IMSIC of the targeted hart.
  genmsi_address = (aplic_baseaddress + GENMSI_OFFSET);
  genmsi_value = ((index << 18) | (0 << 12) | (EEID));

  val_print(ACS_PRINT_INFO, "\n       genmsi value - 0x%lx", genmsi_value);
  val_mmio_write(genmsi_address, genmsi_value);

  value = val_mmio_read(genmsi_address); //Read to ensure write is complete
  if (value != genmsi_value)
  {
    val_print(ACS_PRINT_ERR, "\n      GENMSI_READ - 0x%lx", value);
    val_set_status(index, RESULT_FAIL(TEST_NUM, 5));
    return;
  }

  //STEP 7: Verify that the guest index field of the target[i] registers support all values between 0 and GEILEN supported by the IMSIC.
  
  ex_int_srcs = val_gic_get_external_interrupt_sources();

  /* Following block is to determine the GEILEN */
  for (vgein = 1; vgein <= 0x3F; vgein++)
  {
    val_w = (val_hart_get_hstatus() & (~HSTATUS_VGEIN)) | (vgein << HSTATUS_VGEIN_SHIFT);
    val_hart_set_hstatus(val_w);
    val_r = val_hart_get_hstatus();
    if ((val_w & HSTATUS_VGEIN) != (val_r & HSTATUS_VGEIN))
    {
      break;
    }
  }

  val_print(ACS_PRINT_INFO, "\n       Valid VGEIN range is [0, 0x%x]", vgein);
  val_print(ACS_PRINT_INFO, "\n       External Interrupt sources is %d", ex_int_srcs);

  for(uint8_t i = 0; i < ex_int_srcs; i++)
  {
    target_address_i = (aplic_baseaddress + TARGET_ADDRESS_OFFSET + (i*4));
    for(uint8_t j = 0; j < vgein; j++)
    {
      uint32_t target_value = ((index << 18) | (j << 12) | EEID);
      val_mmio_write(target_address_i, target_value);
      value = val_mmio_read(target_address_i);
      if (value != target_value)
      {
        val_print(ACS_PRINT_ERR, "\n       Interrupt targets target[i] write/read mismatch for Guest Index %d", j);
        val_set_status(index, RESULT_FAIL(TEST_NUM, 6));
        return;
      }
    }
  }
  val_set_status(index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_i006_entry(uint32_t num_hart)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_hart = 1;  //This IIC test is run on single processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_hart);

  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_hart, payload, 0);

  /* get the result from all HART and check for failure */
  status = val_check_for_error(TEST_NUM, num_hart, TEST_RULE);

  val_report_status(0, BSA_ACS_END(TEST_NUM), NULL);

  return status;
}
