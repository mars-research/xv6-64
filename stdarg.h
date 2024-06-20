// Limitations of this custom va_list implementation:
//     - Only integer types are managed (since xv6 does not use floats).
//     - va_start must be the first instruction in a variadic function.
//     - The 'first' parameter of va_start must be the first named argument of the function.
//       If there is more than one named parameter, the others should be manually skipped using va_arg.
//     - The function should use all its named parameters if compiled with optimization.
//       Otherwise, the compiler may ignore these parameters. 
//       Note that this does not affect the behavior of the function for the other parameters.

typedef struct {
    // The first six integer arguments are passed through registers (rdi, rsi, rdx, rcx, r8, r9).
    // If there are more than six integer arguments, they are pushed onto the stack.
    // This struct only manages integer variadic functions, since no floats are used by xv6.
    // Floating point arguments are passed through xmm0 to xmm7, and through the stack if there are more than 8 float arguments.
    union {
    struct {
        uint64 rdi, rsi, rdx, rcx, r8, r9;
    };
        uint64 argint[6]; // This array provides a more convenient access to the first six integer arguments.
    };
    uint64 *more_argint;  // Pointer to additional integer arguments on the stack

    uint _index;          // Index to keep track of the current argument
} va_list;

// Initializes the va_list structure with the arguments passed to a function.
// Must be called at the beginning of a variadic function, before any other instruction.
__attribute__((always_inline))
static inline void _va_start(va_list *args)
{
    // Read the registers in one asm volatile block to ensure they are not used for any other purpose before they are read.
    asm volatile("movq %%rdi, %0;" 
                 "movq %%rsi, %1;"
                 "movq %%rdx, %2;"
                 "movq %%rcx, %3;"
                 "movq %%r8, %4;"
                 "movq %%r9, %5;" 
                    :   "=m" (args->rdi),
                        "=m" (args->rsi),
                        "=m" (args->rdx),
                        "=m" (args->rcx),
                        "=m" (args->r8),
                        "=m" (args->r9)
                    : : "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9");

    // Get the address of the additional arguments on the stack.
    // This should be (%rbp + 16) since the old rbp is pushed at the beginning of the function,
    // and the return address is also pushed by the call instruction.
    // Stack layout:
    //          |-----------------|
    //          |      ARG N      |
    //          |-----------------|
    //          |        ...      |
    //          |-----------------|
    //          |      ARG 7      |
    //          |-----------------|
    //          |    RETURN PTR   |
    //          |-----------------|
    //          |      %rbp       |
    //          |-----------------| <- %rbp
    asm volatile("leaq 16(%%rbp), %0" : "=r" (args->more_argint));

    args->_index = 1;
}

// Initializes the va_list structure. 
// 'first' must be the first named argument of the variadic function.
// Must be called at the beginning of a variadic function, before any instruction.
#define va_start(args, first) _va_start(&(args))

// Retrieve the next argument from the va_list.
static inline void *_va_arg(va_list *args)
{
    // Return the next argument from registers if _index < 6, else from the stack.
    return (void *)((args->_index < 6) ? args->argint[args->_index++]
                                       : args->more_argint[(args->_index++) - 6]);
}

// Get the next argument and cast it to the specified type.
#define va_arg(args, type) ({\
            void *_ptr = _va_arg(&(args));\
            type *_type_ptr = (type*)&_ptr;\
            *_type_ptr;})

// End the access to arguments. This is a no-op in this implementation.
#define va_end(args) 