# RTL处理及优化
在[pass.def](../../src/passes.def)中，定义了相关的pass。执行了`pass_expand`之后，GCC的中间表示就已经转换成RTL形式，此后的所有处理过程都是基于RTL的处理过程，即RTL_PASS。所有的RTL处理都包含在`pass_rest_of_compilation`的处理过程及其子过程中，主要包括了对`pass_expand`所生成的insn序列进行进一步的处理，包括循环优化、指令调度、寄存器分配、窥孔优化等过程，并最终根据RTL生成目标机器上的汇编代码。

## 1 pass的实现机制
在[tree-pass.h](../../src/tree-pass.h)中，定义了如下四种类型
```c
/* Optimization pass type.  */
enum opt_pass_type
{
  GIMPLE_PASS,
  RTL_PASS,
  SIMPLE_IPA_PASS,
  IPA_PASS
};
```
结构体`pass_data`，用于保存pass所需要的数据
```c
/* Metadata for a pass, non-varying across all instances of a pass.  */
struct pass_data
{
  /* Optimization pass type.  */
  enum opt_pass_type type;

  /* Terse name of the pass used as a fragment of the dump file
     name.  If the name starts with a star, no dump happens. */
  const char *name;

  /* The -fopt-info optimization group flags as defined in dumpfile.h. */
  optgroup_flags_t optinfo_flags;

  /* The timevar id associated with this pass.  */
  /* ??? Ideally would be dynamically assigned.  */
  timevar_id_t tv_id;

  /* Sets of properties input and output from this pass.  */
  unsigned int properties_required;
  unsigned int properties_provided;
  unsigned int properties_destroyed;

  /* Flags indicating common sets things to do before and after.  */
  unsigned int todo_flags_start;
  unsigned int todo_flags_finish;
};
```

opt_pass定义了优化的pass，它从pass_data继承而来，其中最主要的函数是虚函数`execute`。要想查看一个pass具体做了什么内容，主要就是看他对execute的实现。
```c
/* An instance of a pass.  This is also "pass_data" to minimize the
   changes in existing code.  */
class opt_pass : public pass_data
{
public:
  virtual ~opt_pass () { }

  /* Create a copy of this pass.

     Passes that can have multiple instances must provide their own
     implementation of this, to ensure that any sharing of state between
     this instance and the copy is "wired up" correctly.

     The default implementation prints an error message and aborts.  */
  virtual opt_pass *clone ();
  virtual void set_pass_param (unsigned int, bool);

  /* This pass and all sub-passes are executed only if the function returns
     true.  The default implementation returns true.  */
  virtual bool gate (function *fun);

  /* This is the code to run.  If this is not overridden, then there should
     be sub-passes otherwise this pass does nothing.
     The return value contains TODOs to execute in addition to those in
     TODO_flags_finish.   */
  virtual unsigned int execute (function *fun);

protected:
  opt_pass (const pass_data&, gcc::context *);

public:
  /* A list of sub-passes to run, dependent on gate predicate.  */
  opt_pass *sub;

  /* Next in the list of passes to run, independent of gate predicate.  */
  opt_pass *next;

  /* Static pass number, used as a fragment of the dump file name.  */
  int static_pass_number;

protected:
  gcc::context *m_ctxt;
};
```

其中关于rtl的pass类定义如下：
```c
/* Description of RTL pass.  */
class rtl_opt_pass : public opt_pass
{
protected:
  rtl_opt_pass (const pass_data& data, gcc::context *ctxt)
    : opt_pass (data, ctxt)
  {
  }
};
```

在[pass_manager.h](../../src/pass_manager.h)中，使用了类`pass_manager`来管理所有的pass。在具体创建pass时，是使用make_(pass_name)调用来实现的。

## 2 特殊虚拟寄存器的实例化

在生成RTL过程中，使用了一些特殊的虚拟寄存器，其意义如下:

|name|rtx|作用|
|-|-|-|
|virtual_incoming_args|virtual_incoming_args_rtx|传入参数的基地址|
|virtual_stack_vars|virtual_stack_vars_rtx|自动变量的基地址|
|virtual_stack_dynamic|virtual_stack_dynamic_rtx|堆栈栈顶的地址|
|virtual_outgoing_args|virtual_outgoing_args_rtx|传出参数的基地址|

最终生成代码时，这些虚拟的寄存器需要实例化成目标机器上特定的物理寄存器，`pass_instantiate_virtual_regs`主要作用即是完成这个步骤。

在[function.cc](../../src/function.cc)中，定义了class如下:
```c
const pass_data pass_data_instantiate_virtual_regs =
{
  RTL_PASS, /* type */
  "vregs", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_instantiate_virtual_regs : public rtl_opt_pass
{
public:
  pass_instantiate_virtual_regs (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_instantiate_virtual_regs, ctxt)
  {}

  /* opt_pass methods: */
  unsigned int execute (function *) final override
    {
      instantiate_virtual_regs ();
      return 0;
    }

}; // class pass_instantiate_virtual_regs
```

创建该pass的函数为：
```c
rtl_opt_pass *
make_pass_instantiate_virtual_regs (gcc::context *ctxt)
{
  return new pass_instantiate_virtual_regs (ctxt);
}
```

可以看到在该pass中，最主要的内容即为函数`instantiate_virtual_regs`。

该函数对insn链表中的insn逐一进行如下处理：
+ 如果insn的主体，即`PATTERN (insn)`的RTX_CODE为USE、CLOBBER、ASM_INPUT时不处理，因为这些RTL中不会出现上述的虚拟寄存器。
+ 使用函数`instantiate_virtual_regs_in_insn`及`instantiate_virtual_regs_in_rtx`等对insn中所包含的特殊虚拟寄存器进行实例化
    + `instantiate_virtual_regs_in_insn`同时会使用`extract_insn (insn)`对该insn进行识别，并设置`insn_code = INSN_CODE (insn)`，即与该insn主体部分匹配的指令模板的索引号
+ 其他特殊处理

```c
/* Pass through the INSNS of function FNDECL and convert virtual register
   references to hard register references.  */

static void
instantiate_virtual_regs (void)
{
  rtx_insn *insn;

  /* Compute the offsets to use for this function.  */
  in_arg_offset = FIRST_PARM_OFFSET (current_function_decl);
  var_offset = targetm.starting_frame_offset ();
  dynamic_offset = get_stack_dynamic_offset ();
  out_arg_offset = STACK_POINTER_OFFSET;
#ifdef FRAME_POINTER_CFA_OFFSET
  cfa_offset = FRAME_POINTER_CFA_OFFSET (current_function_decl);
#else
  cfa_offset = ARG_POINTER_CFA_OFFSET (current_function_decl);
#endif

  /* Initialize recognition, indicating that volatile is OK.  */
  init_recog ();

  /* Scan through all the insns, instantiating every virtual register still
     present.  */
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      {
	/* These patterns in the instruction stream can never be recognized.
	   Fortunately, they shouldn't contain virtual registers either.  */
        if (GET_CODE (PATTERN (insn)) == USE
	    || GET_CODE (PATTERN (insn)) == CLOBBER
	    || GET_CODE (PATTERN (insn)) == ASM_INPUT
	    || DEBUG_MARKER_INSN_P (insn))
	  continue;
	else if (DEBUG_BIND_INSN_P (insn))
	  instantiate_virtual_regs_in_rtx (INSN_VAR_LOCATION_PTR (insn));
	else
	  instantiate_virtual_regs_in_insn (insn);

	if (insn->deleted ())
	  continue;

	instantiate_virtual_regs_in_rtx (&REG_NOTES (insn));

	/* Instantiate any virtual registers in CALL_INSN_FUNCTION_USAGE.  */
	if (CALL_P (insn))
	  instantiate_virtual_regs_in_rtx (&CALL_INSN_FUNCTION_USAGE (insn));
      }

  /* Instantiate the virtual registers in the DECLs for debugging purposes.  */
  instantiate_decls (current_function_decl);

  targetm.instantiate_decls ();

  /* Indicate that, from now on, assign_stack_local should use
     frame_pointer_rtx.  */
  virtuals_instantiated = 1;
}
```

对比该过程的处理前后的文件

```c
(insn 7 6 8 2 (set (reg/f:DI 95)
        (const_int 40 [0x28])) "/mnt/c/Users/liqinggui/Desktop/gcc-tutorials/example/gimple2rtl.c":4:1 -1
     (nil))
(insn 8 7 13 2 (parallel [
            (set (mem/v/f/c:DI (plus:DI (reg/f:DI 77 virtual-stack-vars)
                        (const_int -8 [0xfffffffffffffff8])) [2 D.1978+0 S8 A64])
                (unspec:DI [
                        (mem/v/f:DI (reg/f:DI 95) [3 MEM[(<address-space-1> long unsigned int *)40B]+0 S8 A64 AS1])
                    ] UNSPEC_SP_SET))
            (set (scratch:DI)
                (const_int 0 [0]))
            (clobber (reg:CC 17 flags))
        ]) "/mnt/c/Users/liqinggui/Desktop/gcc-tutorials/example/gimple2rtl.c":4:1 -1
     (nil))

############################################################################################################################

(insn 7 6 8 2 (set (reg/f:DI 95)
        (const_int 40 [0x28])) "/mnt/c/Users/liqinggui/Desktop/gcc-tutorials/example/gimple2rtl.c":4:1 74 {*movdi_internal}
     (nil))
(insn 8 7 13 2 (parallel [
            (set (mem/v/f/c:DI (plus:DI (reg/f:DI 19 frame)
                        (const_int -8 [0xfffffffffffffff8])) [2 D.1978+0 S8 A64])
                (unspec:DI [
                        (mem/v/f:DI (reg/f:DI 95) [3 MEM[(<address-space-1> long unsigned int *)40B]+0 S8 A64 AS1])
                    ] UNSPEC_SP_SET))
            (set (scratch:DI)
                (const_int 0 [0]))
            (clobber (reg:CC 17 flags))
        ]) "/mnt/c/Users/liqinggui/Desktop/gcc-tutorials/example/gimple2rtl.c":4:1 1159 {stack_protect_set_1_di}
     (nil))
```

主要变化包括两个方面：
+ 虚拟寄存器（reg/f:DI 77 virtual-stack-vars）被实例化为物理寄存器（reg/f:DI 19 frame）
+ insn中倒数第二个操作数从-1变成了{*movdi_internal},从而确定了该指令对应的insn_code为74

在i386.md文件中，也能找对对应的指令模板定义
```c
(define_insn "*movdi_internal"
  [(set (match_operand:DI 0 "nonimmediate_operand"
    "=r  ,o  ,r,r  ,r,m ,*y,*y,?*y,?m,?r,?*y,?Yv,?v,?v,m ,m,?jc,?*Yd,?r,?v,?*y,?*x,*k,*k  ,*r,*m,*k")
	(match_operand:DI 1 "general_operand"
    "riFo,riF,Z,rem,i,re,C ,*y,Bk ,*y,*y,r  ,C  ,?v,Bk,?v,v,*Yd,jc  ,?v,r  ,*x ,*y ,*r,*kBk,*k,*k,CBC"))]
  "!(MEM_P (operands[0]) && MEM_P (operands[1]))
   && ix86_hardreg_mov_ok (operands[0], operands[1])"
   ...
)
```

## 3 指令调度
GCC中的指令调度就是对当前函数中的insn序列进行重新排序，从而更充分地利用目标机器的硬件资源，提高指令执行的效率。指令调度主要考虑的因素包括数据相关、控制相关、结构i相关、指令延迟或指令代价等，通常指令调度与目标机器中的流水线设置紧密相关。

相关性可以分为以下几类：
+ 数据相关性
    + RAW
    + WAW
    + WAR
    + RAR
+ 控制相关性
+ 结构相关性：在GCC中，通常在机器描述文件中使用`define_automaton`、`define_cpu_unit`及`define_insn_reservation`等RTL语言来描述指令对目标机器上各个硬件资源的占用情况，这些信息在机器文件处理的过程中将用来构造硬件资源描述的自动机，并根据指令对资源的使用情况对指令进行调度，从而避免指令之间的结构相关冲突

GCC中的指令调度主要包括两个处理过程，即`pass_sched`和`pass_sched2`。

### 3.1 指令调度算法
|调度算法|处理过程|源文件|备注|
|:-:|-|-|-|
|swing modulo scheduling|pass_sms|[modulo-sched.cc](../../src/modulo-sched.cc)|在指令调度处理过程之前进行，主要使用该算法对最内层的循环进行处理|
|selective scheduling|pass_sched、pass_sched2|[sel-sched.cc](../../src/sel-sched.cc)|在pass_sched和pass_sched2中均可使用该算法|
|suprtblock scheduling|pass_sched2|[sched-ebb.cc](../../src/sched-ebb.cc)|在pass_sched2中使用该算法|
|list scheduling|pass_sched、pass_sched2|[sched-rgn.cc](../../src/sched-rgn.cc)|如果不指定调度方法，在pass_sched和pass_sched2中默认使用该算法|

表调度算法的基本思想是通过分析指令之间的相关性，包括数据相关性、控制相关性及结构相关性，建立指令之间依赖关系的有向图，计算指令的优先级，使用所有依赖性已经满足的指令构造初始化的就绪指令列表，然后重复以下两个步骤直到就绪指令列表中所有的指令被调度完毕：
+ 从就绪指令列表中按照指令的优先级取出一条指令，该指令所有的依赖关系已经满足
+ 调度该指令，更新与该指令相关的依赖关系，将新的就绪指令插入到就绪指令列表中

在表调度算法中，指令调度的范围通常称为一个区域（region），指令的优先级是指从该指令执行开始，到该区域最后一条指令执行完毕所经历的指令周期数，每条指令的指令周期也称为该指令的指令代价，可使用insn_cost（insn）函数来获取。

### 3.2 指令调度的实现

## 4 统一寄存器分配
RTL生成和处理过程中使用了大量的虚拟寄存器，这些虚拟寄存器在转换成目标机器汇编代码前，需要映射到目标机器中的物理寄存器上，该过程即为寄存器分配。

