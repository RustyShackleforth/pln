/*
 * Copyright (C) 2002-2007 Novamente LLC
 * Copyright (C) 2008 by Singularity Institute for Artificial Intelligence
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <opencog/util/platform.h>
#include "../../PLN.h"

#include "../Rule.h"
#include "../Rules.h"
#include "../../AtomSpaceWrapper.h"
#include "../../PLNatom.h"
#include "../../BackInferenceTreeNode.h"

#include <algorithm>

// Issue: Makes a real link with children in a vtree. -- JaredW
// Issue: i2oType is ambiguous? --JaredW

namespace opencog { namespace pln {

Rule::setOfMPs SimSubstRule1::o2iMetaExtra(meta outh, bool& overrideInputFilter) const
{
/** For simplicity (but sacrificing applicability),
FW_VARs cannot be replaced with children structs.
Links are assumed not inheritable either.
    */
    
    // I'm not sure if any of that is true -- JaredW
    
    if (outh->begin().number_of_children() != 2
        ||  GET_ASW->inheritsType(GET_ASW->getType(boost::get<pHandle>(*outh->begin())), FW_VARIABLE_NODE))
        return Rule::setOfMPs();

/*  puts("X1");
    rawPrint(*outh,0,0);
    puts("X2");*/
    
    Rule::setOfMPs ret;


//  set<atom> child_nodes;
//  find_child_nodes(outh, child_nodes);
    
    Vertex child = CreateVar(asw);
    
    // Should it use different variables for each MPs (MP vector)? -- JaredW
    
    // Makes weird stuff like inheriting from EvaluationLink (with no args) -- JaredW
    // Seems to allow replacing _any_ part of the vtree. Must check that it actually produces
    // the result you need!
    
    // The input is: an inheritance from any part of the output atom,
    // and a version of the output atom that has that atom in it.
    for(tree<Vertex>::pre_order_iterator i = outh->begin(); i != outh->end(); i++)
//  for (set<atom>::iterator i = child_nodes.begin(); i != child_nodes.end(); i++)
    {       
/*      puts("-");
        printAtomTree(outh,0,0);
*/
        Vertex old_i = *i;
        *i = child;
        BBvtree templated_atom1(new BoundVTree(*outh));
        *i = old_i;     
        
        BBvtree inhPattern1(new BoundVTree(mva((pHandle)INHERITANCE_LINK,
            mva(child), mva(*i))));
        
        MPs ret1;
        ret1.push_back(inhPattern1);
        ret1.push_back(templated_atom1);
        
/*      puts("X");
        rawPrint(*outh,0,0);
        puts("-");
        rawPrint(*templated_atom1,0,0);
        puts("-");
        rawPrint(*inhPattern1,0,0);
        puts("-");*/
        
        overrideInputFilter = true;
        
        ret.insert(ret1);
    }
    
    
    return ret;
}

/// fixme This presumably puts a real link with children into a vtree, which is wrong.
/// The problem may even be in the BIT itself.
meta SimSubstRule1::i2oType(const vector<Vertex>& h) const
{
    pHandle h0 = boost::get<pHandle>(h[0]);
    pHandle h1 = boost::get<pHandle>(h[1]);
    
    const int N = h.size();
    assert(2==N);
    assert(asw->getType(h0) == INHERITANCE_LINK);
    
    // ( any, Inh(a,b) )
    
    // Make a vtree out of h1
    meta h1_mp = meta(new vtree(h[1]));
    
    NMPrinter printer(NMP_HANDLE|NMP_TYPE_NAME, 
              NM_PRINTER_DEFAULT_TRUTH_VALUE_PRECISION, 
              NM_PRINTER_DEFAULT_INDENTATION_TAB_SIZE,
			  3);
    printer.print(h1_mp->begin());
    
    // Make it virtual (to get the structure of the link, not just the handle)
    meta ret = ForceAllLinksVirtual(h1_mp);
    
    printer.print(ret->begin());
    
    vector<pHandle> hs = nm->getOutgoing(h0);
    
    // the InhLink (h[0]) is real when this method is called
    // @todo if the child is a link, it will be real. This is OK.

    /// @todo What if child has a different structure to parent?
    std::replace(ret->begin(), ret->end(), Vertex(hs[0]), Vertex(hs[1]));

    printer.print(ret->begin());

    return ret;

#if 0
    atom ret(h1);
    
    //assert(ret.hs[1].real == nm->getOutgoing(h[1])[0]);

    vector<pHandle> hs = nm->getOutgoing(h0);

    /// subst hs[0] to hs[1] (child => parent):
    //ret.hs[0]->substitute(atom(hs[1]), atom(hs[0]));
    ret.substitute(atom(hs[1]), atom(hs[0]));

//printAtomTree(ret,0,0);
    
/*  meta ret(new Tree<Vertex>(mva(nm->getType(boost::get<pHandle>(h[0])),
        mva(nm->getOutgoing(boost::get<pHandle>(h[1]))[0]),
        mva(nm->getOutgoing(boost::get<pHandle>(h[0]))[1])));*/
    
	return BBvtree(new BoundVTree(ret.makeHandletree(asw)));
    // this is the line that crashes the BIT. It should be making a normal vtree out of it (or just using a normal vtree)
#endif
}

}} // namespace opencog { namespace pln {
