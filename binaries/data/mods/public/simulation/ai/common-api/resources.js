Resources = new Resources();

export class ResourcesManager
{
	constructor(amounts = {}, population = 0)
	{
		for (const key of Resources.GetCodes())
			this[key] = amounts[key] || 0;

		this.population = population > 0 ? population : 0;
	}

	reset()
	{
		for (const key of Resources.GetCodes())
			this[key] = 0;
		this.population = 0;
	}

	canAfford(that)
	{
		for (const key of Resources.GetCodes())
			if (this[key] < that[key])
				return false;
		return true;
	}

	add(that)
	{
		for (const key of Resources.GetCodes())
			this[key] += that[key];
		this.population += that.population;
	}

	subtract(that)
	{
		for (const key of Resources.GetCodes())
			this[key] -= that[key];
		this.population += that.population;
	}

	multiply(n)
	{
		for (const key of Resources.GetCodes())
			this[key] *= n;
		this.population *= n;
	}

	Serialize()
	{
		const amounts = {};
		for (const key of Resources.GetCodes())
			amounts[key] = this[key];
		return { "amounts": amounts, "population": this.population };
	}

	Deserialize(data)
	{
		for (const key in data.amounts)
			this[key] = data.amounts[key];
		this.population = data.population;
	}
}
