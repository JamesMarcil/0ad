LoadModificationTemplates();

/** Wrapper around a technology template */
export class Technology
{
	constructor(templateName)
	{
		this._templateName = templateName;
		const template = TechnologyTemplates.Get(templateName);

		// check if this is one of two paired technologies.
		if (template.partOfPair)
		{
			const parentTech = TechnologyTemplates.Get(template.partOfPair);
			this._pairedWith = parentTech.pair[0] == templateName ? parentTech.pair[1] : parentTech.pair[0];
		}

		// check if it only defines a pair:
		this._definesPair = !!template.pair;
		this._template = template;
	}

	/** returns generic, or specific if civ provided. */
	name(civ)
	{
		if (civ === undefined)
			return this._template.genericName;

		if (this._template.specificName === undefined || this._template.specificName[civ] === undefined)
			return undefined;
		return this._template.specificName[civ];
	}

	pairDef()
	{
		return this._definesPair;
	}

	/** in case this defines a pair only, returns the two paired technologies. */
	getPairedTechs()
	{
		if (!this._definesPair)
			return undefined;

		return this._template.pair.map(name => new Technology(name));
	}

	pair()
	{
		return this._template.partOfPair;
	}

	pairedWith()
	{
		if (!this._template.partOfPair)
			return undefined;
		return this._pairedWith;
	}

	cost(researcher)
	{
		if (!this._template.cost)
			return undefined;
		const cost = {};
		for (const type in this._template.cost)
		{
			cost[type] = +this._template.cost[type];
			if (researcher)
				cost[type] *= researcher.techCostMultiplier(type);
		}
		return cost;
	}

	costSum(researcher)
	{
		const cost = this.cost(researcher);
		if (!cost)
			return 0;
		let ret = 0;
		for (const type in cost)
			ret += cost[type];
		return ret;
	}

	researchTime()
	{
		return this._template.researchTime || 0;
	}

	requirements(civ)
	{
		return DeriveTechnologyRequirements(this._template, civ);
	}

	autoResearch()
	{
		if (!this._template.autoResearch)
			return undefined;
		return this._template.autoResearch;
	}

	supersedes()
	{
		if (!this._template.supersedes)
			return undefined;
		return this._template.supersedes;
	}

	modifications()
	{
		if (!this._template.modifications)
			return undefined;
		return this._template.modifications;
	}

	affects()
	{
		if (!this._template.affects)
			return undefined;
		return this._template.affects;
	}

	isAffected(classes)
	{
		return this._template.affects && this._template.affects.some(affect => MatchesClassList(classes, affect));
	}
}
