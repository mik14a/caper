.SUFFIXES: .o .c .cpp .d .hpp
CPPFLAGS	= -D__STDC_LIMIT_MACROS
TARGET		= leaf
OBJS		= $(TARGET).o leaf_node.o leaf_type.o leaf_compile.o
DEPENDDIR	= ./depend
CC = g++

VPATH = ./depend

.c.d:
	$(CC) $(CDBFLG) $(CPPFLAGS) $(MACHINE) $(ENVIRON) -MM $< > $(DEPENDDIR)/$*.d.t
	cp $(DEPENDDIR)/$*.d.t $(DEPENDDIR)/$*.d
	sed -e 's/$*\.o/$*\.d/g' $(DEPENDDIR)/$*.d.t >> $(DEPENDDIR)/$*.d
	rm $(DEPENDDIR)/$*.d.t
	echo '$*.o : $*.d' >> $(DEPENDDIR)/$*.d	

.cpp.d:
	$(CC) $(CDBFLG) $(CPPFLAGS) $(MACHINE) $(ENVIRON) -MM $< > $(DEPENDDIR)/$*.d.t
	cp $(DEPENDDIR)/$*.d.t $(DEPENDDIR)/$*.d
	sed -e 's/$*\.o/$*\.d/g' $(DEPENDDIR)/$*.d.t >> $(DEPENDDIR)/$*.d
	rm $(DEPENDDIR)/$*.d.t
	echo '$*.o : $*.d' >> $(DEPENDDIR)/$*.d	

.cpp.o:
	g++ $(CPPFLAGS) -g -Wall -c -DLINUX -o $@ $<

.c.o:
	gcc $(CPPFLAGS) -g -Wall -c -DLINUX -o $@ $<

all:
	mkdir -p depend
	make depend
	make $(TARGET)

leaf.d : leaf_grammar.hpp leaf_ast.hpp

leaf_grammar.hpp : leaf_grammar.cpg
		caper -c++ leaf_grammar.cpg leaf_grammar.hpp

leaf_ast.hpp : leaf_ast.cap
		capella leaf_ast.cap leaf_ast.hpp

rtl/leaf_rt.bc : rtl/leaf_rt.ll
		llvm-as -f rtl/leaf_rt.ll

depend: $(OBJS:.o=.d)

$(TARGET): $(OBJS) leaf_grammar.hpp leaf_ast.hpp
	$(CC) $(CPPFLAGS) -g -o $@ $(OBJS) -lLLVMCore -lLLVMBitWriter -lLLVMSupport -lLLVMSystem -ldl
clean:
	rm -f $(TARGET) $(OBJS) *.bc
	rm -fr $(DEPENDDIR)
	rm -f leaf_grammar.hpp leaf_ast.hpp
	rm -f rtl/*.bc
	rm -f tsuite/*.ll tsuite/*.bc tsuite/*.result tsuite/*.error

clean_tsuite:
	rm -f tsuite/*.result tsuite/*.ll tsuite/*.bc

test : leaf rtl/leaf_rt.bc
		chmod 755 dotest
		./dotest foo0000
		./dotest foo0001
		./dotest foo0002
		./dotest foo0003
		./dotest foo0004
		./dotest foo0005
		./failtest bar0000
		./failtest bar0001
		./failtest bar0002

try : leaf rtl/leaf_rt.bc
		chmod 755 failtest
		./failtest bar0002

-include $(addprefix $(DEPENDDIR)/,$(OBJS:.o=.d))