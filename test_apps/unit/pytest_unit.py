import pytest
from pytest_embedded import Dut

@pytest.mark.esp32
def test_unit(dut: Dut) -> None:
    dut.run_all_single_board_cases()