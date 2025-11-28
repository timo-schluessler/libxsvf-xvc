#include <stdio.h>
#include <stdint.h>

int main()
{
	const uint8_t cmds[] = {
		0x07, 0x00, // XREPEAT = 0
		0x12, 0x00, // XSTATE -> goto reset
		0x12, 0x01, // XSTATE -> goto RunTest/Idle
		0x04, 0x00, 0x00, 0x00, 0x00, // XRUNTEST -> run 0 cycles in run-test/idle after shift operations

		0x02, 0x06, 0x03, // XSIR -> IR length 6 bits, user 2 cmd 0x3
		0x08, 0x00, 0x00, 0x00, 0x01, // XSDRSIZE -> 1 bit user 2 register size
		0x01, 0x00, // XTDOMASK -> don't care TDO output
		0x09, 0x01, 0x00, // XSDRTDO -> TDO = 1 to switch, TDI don't care
		0x00, // XCOMPLETE
	};
	for (int i = 0; i < sizeof(cmds); i++)
		printf("%c", (char)cmds[i]);
}
