#include "val/include/bsa_acs_val.h"
#include "val/include/val_interface.h"
#include "val/include/bsa_acs_memory.h"

#include "val/include/bsa_acs_iommu.h"

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>

#define SATP_MODE_SV39_temp   (8ULL << 60)   // MODE=8 for Sv39
#define SATP_MODE_SV48_1   (9ULL << 60)   // MODE=9 for Sv48
#define SATP_MODE_SV57_1   (10ULL << 60)  // MODE=10 for Sv57

#define TEST_NUM   (ACS_IOMMU_TEST_NUM_BASE + 2)
#define TEST_RULE  "ME_IOM_050_010"
#define TEST_DESC  "Check page based virtual memory system modes supported by the IOMMU are enumerated in the IOMMU capabilities register."


/**
 * @brief For each application processor hart:
 * 1. Parse ISA string in ACPI RHCT table and determine the page based virtual 
 *    memory systems supported by the harts..
 * 2. For each IOMMU in reported:
 *    a. Verify that the capabilities register enumerates support for each of the 
 *       page based virtual memory system modes supported by the harts.
 */
static
void
payload()
{
  uint32_t index;
  char8_t * isa_string;
  char8_t *ptr;
  
  uint32_t hart_index = val_hart_get_index_mpid(val_hart_get_mpid());
  int32_t iommu_num = val_iommu_get_num();
  uint64_t base_addr, reg_cap;

  /* Parse ISA string in ACPI RHCT table and determine the page based virtual memory systems supported by the harts.*/
  isa_string = val_hart_get_isa_string(hart_index);

  if (isa_string == NULL) {
      val_print(ACS_PRINT_ERR, "\n       Unable to get ISA string", 0);
      val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
      return;
  }

  val_print_test_start(isa_string);
  ptr = val_strstr(isa_string, "svpbmt");
  if (ptr == NULL) {
    val_print(ACS_PRINT_ERR, "\n       svpbmt not found", 0);
    val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
    return;
  }


  for (index = 0; index < iommu_num; index++) {
    if (val_iommu_get_info(index, IOMMU_INFO_TYPE) == EFI_ACPI_6_5_RIMT_DEVICE_TYPE_IOMMU)
    {
      base_addr = val_iommu_get_info(index, IOMMU_INFO_BASE_ADDRESS);

      /* Map the IOMMU memory-mapped register region */
      val_print(ACS_PRINT_INFO, "\n       IOMMU base: 0x%lx", base_addr);
      val_memory_map_add_mmio(base_addr, 0x1000);

      reg_cap = val_mmio_read64(base_addr + 0x0);
      val_print(ACS_PRINT_INFO, "\n       IOMMU reg_cap - 0x%lx", reg_cap);

      if (!(reg_cap & 0x100))
      {
        val_print(ACS_PRINT_ERR, "\n       IOMMU does not support Sv32", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }

      if (!(reg_cap & 0x200))
      {
        val_print(ACS_PRINT_ERR, "\n       IOMMU does not support Sv39", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }

      if (!(reg_cap & 0x400))
      {
        val_print(ACS_PRINT_ERR, "\n       IOMMU does not support Sv48", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }

      if (!(reg_cap & 0x800))
      {
        val_print(ACS_PRINT_ERR, "\n       IOMMU does not support Sv57", 0);
        val_set_status(hart_index, RESULT_FAIL(TEST_NUM, 1));
        return;
      }
    }
  }

  val_set_status(hart_index, RESULT_PASS(TEST_NUM, 1));
}

uint32_t
os_iom005_entry(uint32_t num_hart)
{

  uint32_t status = ACS_STATUS_FAIL;

  num_hart = 1;  //This IOMMU test is run on single processor
  // TODO: test all processor

  status = val_initialize_test(TEST_NUM, TEST_DESC, num_hart);

  if (status != ACS_STATUS_SKIP)
      val_run_test_payload(TEST_NUM, num_hart, payload, 0);

  /* get the result from all HART and check for failure */
  status = val_check_for_error(TEST_NUM, num_hart, TEST_RULE);

  val_report_status(0, BSA_ACS_END(TEST_NUM), NULL);

  return status;
}
