#include "ImportanceUpdatingAgent.h"
#include <mt19937ar.h>
#include <math.h>
#include <time.h>

namespace opencog {

ImportanceUpdatingAgent::ImportanceUpdatingAgent()
{
    /* init starting wages/rents. these should quickly change and reach
     * stable cycles */
    STIAtomRent = DEFAULT_ATOM_STI_RENT;
    LTIAtomRent = DEFAULT_ATOM_LTI_RENT;
    STIAtomWage = DEFAULT_ATOM_STI_WAGE;
    LTIAtomWage = DEFAULT_ATOM_LTI_WAGE;

    updateLinks = true;

    noiseOn = false;
    noiseOdds = 0.20;
    noiseUnit = 10;

    recentTotalStimulusSinceReset = 0;
    recentTotalStimulusDecay = 0.3;

    attentionalFocusSize = 0;
    recentAttentionalFocusSize = 0;
    recentAttentionalFocusNodesSize = 0;
    attentionalFocusSizeDecay = 0.3;

    maxSTIDecayRate = 0.8;
    recentMaxSTI = 0;

    targetLobeSTI = LOBE_STARTING_STI_FUNDS;
    acceptableLobeSTIRange[0] = targetLobeSTI - LOBE_STI_FUNDS_BUFFER;
    acceptableLobeSTIRange[1] = targetLobeSTI + LOBE_STI_FUNDS_BUFFER;
    targetLobeLTI = LOBE_STARTING_LTI_FUNDS;
    acceptableLobeLTIRange[0] = targetLobeLTI - LOBE_LTI_FUNDS_BUFFER;
    acceptableLobeLTIRange[1] = targetLobeLTI + LOBE_LTI_FUNDS_BUFFER;

    lobeSTIOutOfBounds = false;

    STICap = AttentionValue::MAXSTI / 2;
    LTICap = AttentionValue::MAXLTI / 2;

    initialEstimateMade = false;

    rng = NULL;

    // Provide a logger, but disable it initially
    setLogger(new Util::Logger("ImportanceUpdatingAgent.log",Util::Logger::DEBUG,true));
    log->disable();
}

ImportanceUpdatingAgent::~ImportanceUpdatingAgent()
{
    if (log) delete log;
    if (rng) delete rng;
}
		
void ImportanceUpdatingAgent::init(CogServer *server)
{
    /* Not sure exactly what initial estimates should be made... */
    log->log(Util::Logger::FINE, "ImportanceUpdatingAgent::init");
    initialEstimateMade = true;

}

void ImportanceUpdatingAgent::setLogger(Util::Logger* log)
{
    if (this->log) delete this->log;
    this->log = log;
    log->log(Util::Logger::FINE, "Set new logger for ImportanceUpdatingMindAgent");
}

Util::Logger* ImportanceUpdatingAgent::getLogger()
{
    return log;
}

void ImportanceUpdatingAgent::run(CogServer *server)
{
    AtomSpace* a = server->getAtomSpace();
    HandleEntry *h, *q;
   
    log->log(Util::Logger::FINE, "=========== ImportanceUpdating::run =======");
    /* init iterative variables, that can't be calculated in
     * (no pointer to CogServer there) */
    if (!initialEstimateMade) init(server);

    /* Calculate attentional focus sizes */
    log->log(Util::Logger::DEBUG, "Updating attentional focus size");
    updateAttentionalFocusSizes(a);

    /* Check AtomSpace funds are within bounds */
    log->log(Util::Logger::DEBUG, "Checking AtomSpace funds");
    checkAtomSpaceFunds(a);

    /* Random stimulation if on */
    if (noiseOn) {
	log->log(Util::Logger::DEBUG, "Random stimulation on, stimulating atoms");
	randomStimulation(a);
    }

    /* Update stimulus totals */
    updateTotalStimulus(a);

    /* Update atoms: Collect rent, pay wages */
    log->log(Util::Logger::DEBUG, "Collecting rent and paying wages");

    h = a->getAtomTable().getHandleSet(ATOM, true);
    q=h;
    while (q) {
	updateAtomSTI(a, q->handle);
	updateAtomLTI(a, q->handle);
    
	/* Enfore sti and lti caps */
	enforceSTICap(a, q->handle);
	enforceLTICap(a, q->handle);

	q = q->next;
    }
    delete h;

    if (lobeSTIOutOfBounds) {
	log->log(Util::Logger::DEBUG, "Lobe STI was out of bounds, updating STI rent");
	updateSTIRent(a);
    }

    /* Reset Stimulus */
    a->resetStimulus();

}

void ImportanceUpdatingAgent::updateTotalStimulus(AtomSpace* a)
{
    double r = (double) recentTotalStimulusDecay;
    recentTotalStimulusSinceReset = (stim_t) (r * a->getTotalStimulus() + (1.0-r) \
				     * recentTotalStimulusSinceReset);

}

void ImportanceUpdatingAgent::setNoiseFlag(bool newVal)
{
    noiseOn = newVal;
}

bool ImportanceUpdatingAgent::inRange(long val, long range[2]) const
{
    if (val <= range[1] && val >= range[0])
	return true;
    return false;
}

void ImportanceUpdatingAgent::checkAtomSpaceFunds(AtomSpace* a)
{
    log->log(Util::Logger::DEBUG, "STI funds = %d, range=[%d,%d]", a->getSTIFunds(),
	    acceptableLobeSTIRange[0], acceptableLobeSTIRange[1]);
    if (!inRange(a->getSTIFunds(),acceptableLobeSTIRange)) {
	log->log(Util::Logger::DEBUG, "Lobe STI funds out of bounds, re-adjusting.");
	lobeSTIOutOfBounds = true;
	adjustSTIFunds(a);
    }

    log->log(Util::Logger::DEBUG, "LTI funds = %d, range=[%d,%d]", a->getLTIFunds(),
	    acceptableLobeLTIRange[0], acceptableLobeLTIRange[1]);
    if (!inRange(a->getLTIFunds(),acceptableLobeLTIRange)) {
	log->log(Util::Logger::DEBUG, "Lobe LTI funds out of bounds, re-adjusting.");
	adjustLTIFunds(a);
    }
}

Util::RandGen* ImportanceUpdatingAgent::getRandGen()
{
    if (!rng) {
	// TODO: Use time or something
	rng = new Util::MT19937RandGen(time(NULL));
    }
    return rng;
}

void ImportanceUpdatingAgent::randomStimulation(AtomSpace* a)
{
    int expectedNum, actualNum;    
    HandleEntry *h, *q;
    Util::RandGen *rng;

    rng = getRandGen();

    log->log(Util::Logger::FINE, "Starting random stimulation");

    expectedNum = (int) (noiseOdds * a->getAtomTable().getSize());

    // TODO: use util::lazy_random_selector and a binomial dist
    // to get actualNum
    actualNum = 0;
    h = a->getAtomTable().getHandleSet(ATOM, true);
    q=h;
    while (q) {
	double r;
	r=rng->randdouble();
	if (r < noiseOdds) {
	    a->stimulateAtom(q->handle, noiseUnit);
	    actualNum++;
	}
	q = q->next;
    }

    log->log(Util::Logger::INFO, "Applied stimulation randomly to %d " \
	    "atoms, expected about %d.", actualNum, expectedNum);

    delete h;

}

void ImportanceUpdatingAgent::adjustSTIFunds(AtomSpace* a)
{
    long diff, oldTotal;
    AttentionValue::sti_t afterTax,beforeTax;
    double taxAmount;
    HandleEntry* h;
    HandleEntry* q;

    oldTotal = a->getSTIFunds();
    diff = targetLobeSTI - oldTotal;
    h = a->getAtomTable().getHandleSet(ATOM, true);
    taxAmount = (double) diff / (double) a->getAtomTable().getSize();

    q=h;
    while (q) {
	int actualTax;
	actualTax = getTaxAmount(taxAmount);
	beforeTax = a->getSTI(q->handle);
	afterTax = beforeTax - actualTax;
	a->setSTI(q->handle, afterTax);
	log->log(Util::Logger::FINE, "sti %d. Actual tax %d. after tax %d.", beforeTax, actualTax, afterTax); 
	q = q->next;
    }
    delete h;

    log->log(Util::Logger::INFO, "AtomSpace STI Funds were %d, now %d. All atoms taxed %f.", \
	    oldTotal, a->getSTIFunds(), taxAmount);
    
}

void ImportanceUpdatingAgent::adjustLTIFunds(AtomSpace* a)
{
    long diff, oldTotal;
    AttentionValue::lti_t afterTax;
    double taxAmount;
    HandleEntry* h;
    HandleEntry* q;

    oldTotal = a->getLTIFunds();
    diff = targetLobeLTI - oldTotal;
    h = a->getAtomTable().getHandleSet(ATOM, true);

    taxAmount = (double) diff / (double) a->getAtomTable().getSize();

    q=h;
    while (q) {
	afterTax = a->getLTI(q->handle) - getTaxAmount(taxAmount);
	a->setLTI(q->handle, afterTax);
	q = q->next;
    }
    delete h;
    
    log->log(Util::Logger::INFO, "AtomSpace LTI Funds were %d, now %d. All atoms taxed %.2f.", \
	    oldTotal, a->getLTIFunds(), taxAmount);
}

int ImportanceUpdatingAgent::getTaxAmount(double mean)
{
    double sum, prob, p;
    int count = 0;
    bool negative = false;

    if (mean < 0.0) {
	negative = true;
	mean = -mean;
    }

    // Calculates tax amount by sampling a Poisson distribution
    p = getRandGen()->randDoubleOneExcluded();
    prob = sum = exp(-mean);

    if (sum == 0.0f) {
	log->log(Util::Logger::WARNING, "Mean (%.4f) for Poisson too large!", mean);
    }

    while (p > sum) {
	count++;
	prob = (prob*mean)/count;
	sum += prob;
    }

    if (negative) count = -count;

    return count;
}

void ImportanceUpdatingAgent::updateSTIRent(AtomSpace* a)
{
    AttentionValue::sti_t oldSTIAtomRent;
    // STIAtomRent must be adapted based on attentional focus size, or else balance btw
    // lobe STI wealth and node/link STI wealth may not be maintained

    oldSTIAtomRent = STIAtomRent;
    
    if (!updateLinks) {
	if (recentAttentionalFocusNodesSize > 0)
	    STIAtomRent = (AttentionValue::sti_t) ceil((float) STIAtomWage * (float) recentTotalStimulusSinceReset \
			  / (float) recentAttentionalFocusNodesSize);
	else
	    STIAtomRent = (AttentionValue::sti_t)ceil((float) STIAtomWage * (float) recentTotalStimulusSinceReset);
    } else {
	if (recentAttentionalFocusSize > 0)
	    STIAtomRent = (AttentionValue::sti_t)ceil((float) STIAtomWage * (float) recentTotalStimulusSinceReset \
			  / (float) recentAttentionalFocusSize);
	else
	    STIAtomRent = (AttentionValue::sti_t)ceil((float) STIAtomWage * (float) recentTotalStimulusSinceReset);
    }

    log->log(Util::Logger::INFO, "STIAtomRent was %d, now %d.", oldSTIAtomRent, STIAtomRent);

    lobeSTIOutOfBounds = false; 
}
    

void ImportanceUpdatingAgent::updateAttentionalFocusSizes(AtomSpace* a)
{
    float r = attentionalFocusSizeDecay;
    int n = 0;
    HandleEntry* inFocus;
    HandleEntry* h;

    const AtomTable& at = a->getAtomTable(); 
    // TODO: implement max and get method of next line
    inFocus = at.getHandleSet(a->getAttentionalFocusBoundary(),AttentionValue::MAXSTI);
    attentionalFocusSize = inFocus->getSize();

    recentAttentionalFocusSize = (long) ( (r * attentionalFocusSize) + \
				 ((1.0-r) * recentAttentionalFocusSize) );
  
    log->log(Util::Logger::FINE, "attentionalFocusSize = %d, recent = %d",
	    attentionalFocusSize, recentAttentionalFocusSize);

    h = inFocus;
    while (h) {
	if (a->isNode(h->getAtom()->getType()))
	    n += 1;
	h = h->next;
    }
    attentionalFocusNodesSize = n;
    recentAttentionalFocusNodesSize = (long) ( (r * attentionalFocusNodesSize) + \
				 ((1.0-r) * recentAttentionalFocusNodesSize) );

    log->log(Util::Logger::FINE, "attentionalFocusNodesSize = %d, recent = %d",
	    attentionalFocusNodesSize, recentAttentionalFocusNodesSize);

    delete inFocus;

}

void ImportanceUpdatingAgent::updateAtomSTI(AtomSpace* a, Handle h)
{
    AttentionValue::sti_t current, stiRentCharged, exchangeAmount;
    stim_t s;

    current = a->getSTI(h);
    /* collect if STI > a->attentionalFocusBoundary */
    if (current > a->getAttentionalFocusBoundary())
	stiRentCharged = STIAtomRent;
    else
	stiRentCharged = 0;

    s = a->getAtomStimulus(h);
    exchangeAmount = - stiRentCharged + (STIAtomWage * s);
    a->setSTI(h, current + exchangeAmount);

    log->log(Util::Logger::FINE, "Atom %s stim = %d, STI old = %d, new = %d", a->getName(h).c_str(), s, current, a->getSTI(h));

}

void ImportanceUpdatingAgent::updateAtomLTI(AtomSpace* a, Handle h)
{
    /* collect LTI */
    AttentionValue::lti_t current, exchangeAmount;

    current = a->getLTI(h);
    exchangeAmount = - LTIAtomRent + (LTIAtomWage * a->getAtomStimulus(h));
    a->setLTI(h, current + exchangeAmount);

    log->log(Util::Logger::FINE, "Atom %s LTI old = %d, new = %d", a->getName(h).c_str(), current, a->getLTI(h));
}

bool ImportanceUpdatingAgent::enforceSTICap(AtomSpace* a, Handle h)
{
    AttentionValue::sti_t current, diff;

    current = a->getSTI(h);
    if (current > STICap) {
	diff = current - STICap;
	a->setSTI(h, STICap);
	log->log(Util::Logger::FINE, "Atom STI too high - old = %d, new = %d", current, a->getSTI(h));
	return true;
    } else if (current < -STICap) {
	diff = -STICap + current;
	a->setSTI(h, -STICap);
	log->log(Util::Logger::FINE, "Atom STI too low - old = %d, new = %d", current, a->getSTI(h));
	return true;
    }
    return false;
}

bool ImportanceUpdatingAgent::enforceLTICap(AtomSpace* a, Handle h)
{
    AttentionValue::lti_t current, diff;

    current = a->getLTI(h);
    if (current > LTICap) {
	diff = current - LTICap;
	a->setLTI(h, LTICap);
	log->log(Util::Logger::FINE, "Atom LTI too high - old = %d, new = %d", current, a->getSTI(h));
	return true;
    } else if (current < -LTICap) {
	diff = -LTICap + current;
	a->setLTI(h, -LTICap);
	log->log(Util::Logger::FINE, "Atom LTI too low - old = %d, new = %d", current, a->getSTI(h));
	return true;
    }
    return false;
}

string ImportanceUpdatingAgent::toString()
{
    ostringstream s;

    s << "Importance Updating Mind Agent\n";
    s << "STIAtomRent: " << STIAtomRent << "\n";
    s << "STIAtomWage: " << STIAtomWage << "\n";
    s << "LTIAtomRent: " << LTIAtomRent << "\n";
    s << "LTIAtomWage: " << LTIAtomWage << "\n";
    s << "AV Caps (STI/LTI): " << STICap << "/" << LTICap << "\n";
    s << "Updating Links: ";
    if (updateLinks) s <<  "Yes";
    else s << "No";
    s << "\n";
    if (noiseOn)
	s << "Random stimulation on. Chance: " << noiseOdds << \
	    " Amount: " << noiseUnit << "\n";
    s << "Recent Total Stim since reset: " << recentTotalStimulusSinceReset \
	<< ", decay: " << recentTotalStimulusDecay << "\n";
    s << "Att. focus. Size: " << attentionalFocusSize << ", recent: " \
	<< recentAttentionalFocusSize << ", recentForNodes: " \
	<< recentAttentionalFocusNodesSize << ", decay: " \
	<< attentionalFocusSizeDecay << "\n"; 
    s << "target (range) STI: " << targetLobeSTI << \
	"(" << acceptableLobeSTIRange[0] << "-" << acceptableLobeSTIRange[1] << \
	") LTI: " << targetLobeLTI << \
	"(" << acceptableLobeLTIRange[0] << "-" << acceptableLobeLTIRange[1] << \
	")\n";

    s.put(0); //null terminate the string cout
    return s.str();

}

} // Namespace opencog
