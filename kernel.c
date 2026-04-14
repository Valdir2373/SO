void kmain() {
    char* vga = (char*) 0xB8000;

    for (int i = 0; i < 80 * 25 * 2; i += 2) {
        vga[i] = ' ';    
        vga[i+1] = 0x07;  
    }

    vga[0] = 'H'; vga[1] = 0x0A;
    vga[2] = 'e'; vga[3] = 0x0A;
    vga[4] = 'l'; vga[5] = 0x0A;
    vga[6] = 'l'; vga[7] = 0x0A;
    vga[8] = 'o'; vga[9] = 0x0A;
}