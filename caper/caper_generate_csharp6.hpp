#ifndef CAPER_GENERATE_CSHARP6_HPP
#define CAPER_GENERATE_CSHARP6_HPP

#include "caper_ast.hpp"

void generate_csharp6(
    const std::string&                  src_filename,
    std::ostream&                       os,
    const GenerateOptions&              options,
    const std::map<std::string, Type>&  terminal_types,
    const std::map<std::string, Type>&  nonterminal_types,
    const std::vector<std::string>&     tokens,
    const action_map_type&              actions,
    const tgt::parsing_table&           table);

#endif // CAPER_GENERATE_CSHARP6_HPP
