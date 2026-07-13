import { Template } from "simulation/ai/common-api/entity.js";
import * as filters from "simulation/ai/common-api/filters.js";
import { ResourcesManager } from "simulation/ai/common-api/resources.js";
import { Technology } from "simulation/ai/common-api/technology.js";

/**
 * Provides an API for the rest of the AI scripts to query the world state at a
 * higher level than the raw data.
 */
export class GameState
{
	ai = null; // must be updated by the AIs.

	init(SharedScript, state, player)
	{
		this.sharedScript = SharedScript;
		this.EntCollecNames = SharedScript._entityCollectionsName;
		this.timeElapsed = SharedScript.timeElapsed;
		this.circularMap = SharedScript.circularMap;
		this.templates = SharedScript._templates;
		this.entities = SharedScript.entities;
		this.player = player;
		this.playerData = SharedScript.playersData[this.player];
		this.victoryConditions = SharedScript.victoryConditions;
		this.alliedVictory = SharedScript.alliedVictory;
		this.ceasefireActive = SharedScript.ceasefireActive;
		this.ceasefireTimeRemaining = SharedScript.ceasefireTimeRemaining;

		// get the list of possible phases for this civ:
		// we assume all of them are researchable from the civil center
		this.phases = [];
		const cctemplate = this.getTemplate(this.applyCiv("structures/{civ}/civil_centre"));
		if (!cctemplate)
			return;
		const civ = this.getPlayerCiv();
		const techs = cctemplate.researchableTechs(this, civ);

		const phaseData = {};
		const phaseMap = {};
		for (let techName of techs)
		{
			if (!techName.startsWith("phase"))
				continue;
			let techData = this.getTemplate(techName);

			if (techData._definesPair)
			{
				// Cannot call pickrandom because this function is called on rejoin and that causes oos.
				// (reverting rP20750)
				techName = this.playerData.disabledTechnologies[techData._template.pair[0]] ?
					techData._template.pair[1] : techData._template.pair[0];

				const supersedes = techData._template.supersedes;
				techData = clone(this.getTemplate(techName));
				if (supersedes)
					techData._template.supersedes = supersedes;
			}

			phaseData[techName] = GetTechnologyBasicDataHelper(techData._template, civ);
			if (phaseData[techName].replaces)
				phaseMap[phaseData[techName].replaces[0]] = techName;
		}

		this.phases = UnravelPhases(phaseData).map(phaseName => ({
			"name": phaseMap[phaseName] || phaseName,
			"requirements": phaseMap[phaseName] ? phaseData[phaseMap[phaseName]].reqs : []
		}));
	}

	update(SharedScript)
	{
		this.timeElapsed = SharedScript.timeElapsed;
		this.playerData = SharedScript.playersData[this.player];
		this.ceasefireActive = SharedScript.ceasefireActive;
		this.ceasefireTimeRemaining = SharedScript.ceasefireTimeRemaining;
	}

	updatingCollection(id, filter, parentCollection)
	{
		const gid = "player-" + this.player + "-" + id;	// automatically add the player ID
		return this.updatingGlobalCollection(gid, filter, parentCollection);
	}

	destroyCollection(id)
	{
		const gid = "player-" + this.player + "-" + id;	// automatically add the player ID
		this.destroyGlobalCollection(gid);
	}

	updatingGlobalCollection(gid, filter, parentCollection)
	{
		if (this.EntCollecNames.has(gid))
			return this.EntCollecNames.get(gid);

		const collection = parentCollection ? parentCollection.filter(filter) : this.entities.filter(filter);
		collection.registerUpdates();
		this.EntCollecNames.set(gid, collection);
		return collection;
	}

	destroyGlobalCollection(gid)
	{
		if (!this.EntCollecNames.has(gid))
			return;

		this.sharedScript.removeUpdatingEntityCollection(this.EntCollecNames.get(gid));
		this.EntCollecNames.delete(gid);
	}

	/**
	 * Reset the entities collections which depend on diplomacy
	 */
	resetOnDiplomacyChanged()
	{
		for (const name of this.EntCollecNames.keys())
			if (name.startsWith("player-" + this.player + "-diplo"))
				this.destroyGlobalCollection(name);
	}

	getTimeElapsed()
	{
		return this.timeElapsed;
	}

	getBarterPrices()
	{
		return this.playerData.barterPrices;
	}

	getVictoryConditions()
	{
		return this.victoryConditions;
	}

	getAlliedVictory()
	{
		return this.alliedVictory;
	}

	isCeasefireActive()
	{
		return this.ceasefireActive;
	}

	getTemplate(type)
	{
		if (TechnologyTemplates.Has(type))
			return new Technology(type);

		if (this.templates[type] === undefined)
			this.sharedScript.GetTemplate(type);

		return this.templates[type] ? new Template(this.sharedScript, type, this.templates[type]) : null;
	}

	/** Return the template of the structure built from this foundation */
	getBuiltTemplate(foundationName)
	{
		if (!foundationName.startsWith("foundation|"))
		{
			warn("Foundation " + foundationName + " not recognized as a foundation.");
			return null;
		}
		return this.getTemplate(foundationName.substr(11));
	}

	applyCiv(str)
	{
		return str.replace(/\{civ\}/g, this.playerData.civ);
	}

	getPlayerCiv(player)
	{
		return player !== undefined ? this.sharedScript.playersData[player].civ : this.playerData.civ;
	}

	currentPhase()
	{
		for (let i = this.phases.length; i > 0; --i)
			if (this.isResearched(this.phases[i-1].name))
				return i;
		return 0;
	}

	getNumberOfPhases()
	{
		return this.phases.length;
	}

	getPhaseName(i)
	{
		return this.phases[i-1] ? this.phases[i-1].name : undefined;
	}

	getPhaseEntityRequirements(i)
	{
		const entityReqs = [];

		for (const requirement of this.phases[i-1].requirements)
		{
			if (!requirement.entities)
				continue;
			for (const entity of requirement.entities)
				if (entity.check == "count")
					entityReqs.push({
						"class": entity.class,
						"count": entity.number
					});
		}

		return entityReqs;
	}

	isResearched(template)
	{
		return this.playerData.researchedTechs.has(template);
	}

	isResearching(template)
	{
		return this.playerData.researchQueued.has(template);
	}

	/** this is an "in-absolute" check that doesn't check if we have a building to research from. */
	canResearch(techTemplateName, noRequirementCheck)
	{
		if (this.playerData.disabledTechnologies[techTemplateName])
			return false;

		const template = this.getTemplate(techTemplateName);
		if (!template)
			return false;

		if (this.playerData.researchQueued.has(techTemplateName) ||
		    this.playerData.researchedTechs.has(techTemplateName))
			return false;

		if (noRequirementCheck)
			return true;

		// if this is a pair, we must check that the pair tech is not being researched
		if (template.pair())
		{
			const other = template.pairedWith();
			if (this.playerData.researchQueued.has(other) ||
			    this.playerData.researchedTechs.has(other))
				return false;
		}

		return this.checkTechRequirements(template.requirements(this.playerData.civ));
	}

	/**
	 * Private function for checking a set of requirements is met.
	 * Basically copies TechnologyManager, but compares against
	 * variables only available within the AI
	 */
	checkTechRequirements(reqs)
	{
		if (!reqs)
			return false;

		if (!reqs.length)
			return true;

		const doesEntitySpecPass = entity =>
		{
			switch (entity.check)
			{
			case "count":
				return this.playerData.classCounts[entity.class] &&
						this.playerData.classCounts[entity.class] >= entity.number;

			case "variants":
				return this.playerData.typeCountsByClass[entity.class] &&
						Object.keys(this.playerData.typeCountsByClass[entity.class]).length >= entity.number;

			default:
				return true;
			}
		};

		return reqs.some(req =>
		{
			return Object.keys(req).every(type =>
			{
				switch (type)
				{
				case "techs":
					return req[type].every(tech => this.playerData.researchedTechs.has(tech));

				case "entities":
					return req[type].every(doesEntitySpecPass);
				default:
					return false;
				}
			});
		});
	}

	getPassabilityMap()
	{
		return this.sharedScript.passabilityMap;
	}

	getPassabilityClassMask(name)
	{
		if (!this.sharedScript.passabilityClasses[name])
			error("Tried to use invalid passability class name '" + name + "'");
		return this.sharedScript.passabilityClasses[name];
	}

	getResources()
	{
		return new ResourcesManager(this.playerData.resourceCounts);
	}

	getPopulation()
	{
		return this.playerData.popCount;
	}

	getPopulationLimit()
	{
		return this.playerData.popLimit;
	}

	getPopulationMax()
	{
		return this.playerData.popMax;
	}

	getPlayerID()
	{
		return this.player;
	}

	hasAllies()
	{
		for (const i in this.playerData.isAlly)
			if (this.playerData.isAlly[i] && +i !== this.player &&
			    this.sharedScript.playersData[i].state !== "defeated")
				return true;
		return false;
	}

	hasEnemies()
	{
		for (const i in this.playerData.isEnemy)
			if (this.playerData.isEnemy[i] && +i !== 0 &&
			    this.sharedScript.playersData[i].state !== "defeated")
				return true;
		return false;
	}

	hasNeutrals()
	{
		for (const i in this.playerData.isNeutral)
			if (this.playerData.isNeutral[i] &&
			    this.sharedScript.playersData[i].state !== "defeated")
				return true;
		return false;
	}

	isPlayerNeutral(id)
	{
		return this.playerData.isNeutral[id];
	}

	isPlayerAlly(id)
	{
		return this.playerData.isAlly[id];
	}

	isPlayerMutualAlly(id)
	{
		return this.playerData.isMutualAlly[id];
	}

	isPlayerEnemy(id)
	{
		return this.playerData.isEnemy[id];
	}

	/** Return the number of players currently enemies, not including gaia */
	getNumPlayerEnemies()
	{
		let num = 0;
		for (let i = 1; i < this.playerData.isEnemy.length; ++i)
			if (this.playerData.isEnemy[i] &&
			    this.sharedScript.playersData[i].state != "defeated")
				++num;
		return num;
	}

	getEnemies()
	{
		const ret = [];
		for (const i in this.playerData.isEnemy)
			if (this.playerData.isEnemy[i])
				ret.push(+i);
		return ret;
	}

	getNeutrals()
	{
		const ret = [];
		for (const i in this.playerData.isNeutral)
			if (this.playerData.isNeutral[i])
				ret.push(+i);
		return ret;
	}

	getAllies()
	{
		const ret = [];
		for (const i in this.playerData.isAlly)
			if (this.playerData.isAlly[i])
				ret.push(+i);
		return ret;
	}

	getExclusiveAllies()
	{	// Player is not included
		const ret = [];
		for (const i in this.playerData.isAlly)
			if (this.playerData.isAlly[i] && +i !== this.player)
				ret.push(+i);
		return ret;
	}

	getMutualAllies()
	{
		const ret = [];
		for (const i in this.playerData.isMutualAlly)
			if (this.playerData.isMutualAlly[i] &&
			    this.sharedScript.playersData[i].isMutualAlly[this.player])
				ret.push(+i);
		return ret;
	}

	isEntityAlly(ent)
	{
		if (!ent)
			return false;
		return this.playerData.isAlly[ent.owner()];
	}

	isEntityExclusiveAlly(ent)
	{
		if (!ent)
			return false;
		return this.playerData.isAlly[ent.owner()] && ent.owner() !== this.player;
	}

	isEntityEnemy(ent)
	{
		if (!ent)
			return false;
		return this.playerData.isEnemy[ent.owner()];
	}

	isEntityOwn(ent)
	{
		if (!ent)
			return false;
		return ent.owner() === this.player;
	}

	getEntityById(id)
	{
		return this.entities._entities.get(+id);
	}

	getEntities(id)
	{
		if (id === undefined)
			return this.entities;

		return this.updatingGlobalCollection("player-" + id + "-entities", filters.byOwner(id));
	}

	getStructures()
	{
		return this.updatingGlobalCollection("structures", filters.byClass("Structure"), this.entities);
	}

	getOwnEntities()
	{
		return this.updatingGlobalCollection("player-" + this.player + "-entities",
			filters.byOwner(this.player));
	}

	getOwnStructures()
	{
		return this.updatingGlobalCollection("player-" + this.player + "-structures",
			filters.byClass("Structure"), this.getOwnEntities());
	}

	getOwnUnits()
	{
		return this.updatingGlobalCollection("player-" + this.player + "-units", filters.byClass("Unit"),
			this.getOwnEntities());
	}

	getAllyEntities()
	{
		return this.entities.filter(filters.byOwners(this.getAllies()));
	}

	getExclusiveAllyEntities()
	{
		return this.entities.filter(filters.byOwners(this.getExclusiveAllies()));
	}

	getAllyStructures(allyID)
	{
		if (allyID == undefined)
		{
			return this.updatingCollection("diplo-ally-structures", filters.byOwners(this.getAllies()),
				this.getStructures());
		}

		return this.updatingGlobalCollection("player-" + allyID + "-structures", filters.byOwner(allyID),
			this.getStructures());
	}

	getNeutralStructures()
	{
		return this.getStructures().filter(filters.byOwners(this.getNeutrals()));
	}

	getEnemyEntities()
	{
		return this.entities.filter(filters.byOwners(this.getEnemies()));
	}

	getEnemyStructures(enemyID)
	{
		if (enemyID === undefined)
		{
			return this.updatingCollection("diplo-enemy-structures", filters.byOwners(this.getEnemies()),
				this.getStructures());
		}

		return this.updatingGlobalCollection("player-" + enemyID + "-structures", filters.byOwner(enemyID),
			this.getStructures());
	}

	getEnemyUnits(enemyID)
	{
		if (enemyID === undefined)
			return this.getEnemyEntities().filter(filters.byClass("Unit"));

		return this.updatingGlobalCollection("player-" + enemyID + "-units", filters.byClass("Unit"),
			this.getEntities(enemyID));
	}

	/** if maintain is true, this will be stored. Otherwise it's one-shot. */
	getOwnEntitiesByMetadata(key, value, maintain)
	{
		if (maintain)
		{
			return this.updatingCollection(key + "-" + value, filters.byMetadata(this.player, key, value),
				this.getOwnEntities());
		}
		return this.getOwnEntities().filter(filters.byMetadata(this.player, key, value));
	}

	getOwnEntitiesByRole(role, maintain)
	{
		return this.getOwnEntitiesByMetadata("role", role, maintain);
	}

	getOwnEntitiesByType(type, maintain)
	{
		const filter = filters.byType(type);
		if (maintain)
			return this.updatingCollection("type-" + type, filter, this.getOwnEntities());
		return this.getOwnEntities().filter(filter);
	}

	getOwnEntitiesByClass(cls, maintain)
	{
		const filter = filters.byClass(cls);
		if (maintain)
			return this.updatingCollection("class-" + cls, filter, this.getOwnEntities());
		return this.getOwnEntities().filter(filter);
	}

	getOwnFoundationsByClass(cls, maintain)
	{
		const filter = filters.byClass(cls);
		if (maintain)
			return this.updatingCollection("foundations-class-" + cls, filter, this.getOwnFoundations());
		return this.getOwnFoundations().filter(filter);
	}

	getOwnTrainingFacilities()
	{
		return this.updatingGlobalCollection("player-" + this.player + "-training-facilities",
			filters.byTrainingQueue(), this.getOwnEntities());
	}

	getOwnResearchFacilities()
	{
		return this.updatingGlobalCollection("player-" + this.player + "-research-facilities",
			filters.byResearchAvailable(this, this.playerData.civ), this.getOwnEntities());
	}


	countEntitiesByType(type, maintain)
	{
		return this.getOwnEntitiesByType(type, maintain).length;
	}

	countEntitiesAndQueuedByType(type, maintain)
	{
		const template = this.getTemplate(type);
		if (!template)
			return 0;

		let count = this.countEntitiesByType(type, maintain);

		// Count building foundations
		if (template.hasClass("Structure") === true)
			count += this.countFoundationsByType(type, true);
		else if (template.resourceSupplyType() !== undefined)	// animal resources
			count += this.countEntitiesByType("resource|" + type, true);
		else
		{
			// Count entities in building production queues
			// TODO: maybe this fails for corrals.
			this.getOwnTrainingFacilities().forEach(function(ent)
			{
				for (const item of ent.trainingQueue())
					if (item.unitTemplate == type)
						count += item.count;
			});
		}

		return count;
	}

	countFoundationsByType(type, maintain)
	{
		const foundationType = "foundation|" + type;

		if (maintain)
		{
			return this.updatingCollection("foundation-type-" + type, filters.byType(foundationType),
				this.getOwnFoundations()).length;
		}

		let count = 0;
		this.getOwnStructures().forEach(function(ent)
		{
			if (ent.templateName() == foundationType)
				++count;
		});
		return count;
	}

	countOwnEntitiesByRole(role)
	{
		return this.getOwnEntitiesByRole(role, "true").length;
	}

	countOwnEntitiesAndQueuedWithRole(role)
	{
		let count = this.countOwnEntitiesByRole(role);

		// Count entities in building production queues
		this.getOwnTrainingFacilities().forEach(function(ent)
		{
			for (const item of ent.trainingQueue())
				if (item.metadata && item.metadata.role && item.metadata.role == role)
					count += item.count;
		});
		return count;
	}

	countOwnQueuedEntitiesWithMetadata(data, value)
	{
		// Count entities in building production queues
		let count = 0;
		this.getOwnTrainingFacilities().forEach(function(ent)
		{
			for (const item of ent.trainingQueue())
				if (item.metadata && item.metadata[data] && item.metadata[data] == value)
					count += item.count;
		});
		return count;
	}

	getOwnFoundations()
	{
		return this.updatingGlobalCollection("player-" + this.player + "-foundations",
			filters.isFoundation(), this.getOwnStructures());
	}

	getOwnDropsites(resource)
	{
		if (resource)
		{
			return this.updatingCollection("ownDropsite-" + resource, filters.isDropsite(resource),
				this.getOwnEntities());
		}
		return this.updatingCollection("ownDropsite-all", filters.isDropsite(), this.getOwnEntities());
	}

	getAnyDropsites(resource)
	{
		if (resource)
			return this.updatingGlobalCollection("anyDropsite-" + resource, filters.isDropsite(resource), this.getEntities());
		return this.updatingGlobalCollection("anyDropsite-all", filters.isDropsite(), this.getEntities());
	}

	getResourceSupplies(resource)
	{
		return this.updatingGlobalCollection("resource-" + resource, filters.byResource(resource),
			this.getEntities());
	}

	getHuntableSupplies()
	{
		return this.updatingGlobalCollection("resource-hunt", filters.isHuntable(), this.getEntities());
	}

	getFishableSupplies()
	{
		return this.updatingGlobalCollection("resource-fish", filters.isFishable(), this.getEntities());
	}

	/** This returns only units from buildings. */
	findTrainableUnits(classes, anticlasses)
	{
		const allTrainable = [];
		const civ = this.playerData.civ;
		this.getOwnTrainingFacilities().forEach(function(ent)
		{
			const trainable = ent.trainableEntities(civ);
			if (!trainable)
				return;
			for (const unit of trainable)
				if (allTrainable.indexOf(unit) === -1)
					allTrainable.push(unit);
		});
		const ret = [];
		const limits = this.getEntityLimits();
		const current = this.getEntityCounts();
		const matchCounts = this.getEntityMatchCounts();
		for (const trainable of allTrainable)
		{
			if (this.isTemplateDisabled(trainable))
				continue;
			const template = this.getTemplate(trainable);
			if (!template || !template.available(this))
				continue;
			const limit = template.matchLimit();
			if (matchCounts && limit && matchCounts[trainable] >= limit)
				continue;
			if (!template.hasClasses(classes) || template.hasClasses(anticlasses))
				continue;
			const category = template.trainingCategory();
			if (category && limits[category] && current[category] >= limits[category])
				continue;

			ret.push([trainable, template]);
		}
		return ret;
	}

	/**
	 * Return all techs which can currently be researched
	 * Does not factor cost.
	 * If there are pairs, both techs are returned.
	 */
	findAvailableTech()
	{
		const allResearchable = [];
		const civ = this.playerData.civ;
		for (const ent of this.getOwnEntities().values())
		{
			const searchable = ent.researchableTechs(this, civ);
			if (!searchable)
				continue;
			for (const tech of searchable)
				if (!this.playerData.disabledTechnologies[tech] && allResearchable.indexOf(tech) === -1)
					allResearchable.push(tech);
		}

		const ret = [];
		for (const tech of allResearchable)
		{
			const template = this.getTemplate(tech);
			if (template.pairDef())
			{
				const techs = template.getPairedTechs();
				if (this.canResearch(techs[0]._templateName))
					ret.push([techs[0]._templateName, techs[0]]);
				if (this.canResearch(techs[1]._templateName))
					ret.push([techs[1]._templateName, techs[1]]);
			}
			else if (this.canResearch(tech))
			{
				// Phases are treated separately
				if (this.phases.every(phase => template._templateName != phase.name))
					ret.push([tech, template]);
			}
		}
		return ret;
	}

	/**
	 * Return true if we have a building able to train that template
	 */
	hasTrainer(template)
	{
		const civ = this.playerData.civ;
		for (const ent of this.getOwnTrainingFacilities().values())
		{
			const trainable = ent.trainableEntities(civ);
			if (trainable && trainable.indexOf(template) !== -1)
				return true;
		}
		return false;
	}

	/**
	 * Find buildings able to train that template.
	 */
	findTrainers(template)
	{
		const civ = this.playerData.civ;
		return this.getOwnTrainingFacilities().filter(function(ent)
		{
			const trainable = ent.trainableEntities(civ);
			return trainable && trainable.indexOf(template) !== -1;
		});
	}

	/**
	 * Get any unit that is capable of constructing the given building type.
	 */
	findBuilder(template)
	{
		const civ = this.getPlayerCiv();
		for (const ent of this.getOwnUnits().values())
		{
			const buildable = ent.buildableEntities(civ);
			if (buildable && buildable.indexOf(template) !== -1)
				return ent;
		}
		return undefined;
	}

	/** Return true if one of our buildings is capable of researching the given tech */
	hasResearchers(templateName, noRequirementCheck)
	{
		// let's check we can research the tech.
		if (!this.canResearch(templateName, noRequirementCheck))
			return false;

		const template = this.getTemplate(templateName);
		if (template.autoResearch())
			return true;

		const civ = this.playerData.civ;

		for (const ent of this.getOwnResearchFacilities().values())
		{
			const techs = ent.researchableTechs(this, civ);
			for (const tech of techs)
			{
				const temp = this.getTemplate(tech);
				if (temp.pairDef())
				{
					const pairedTechs = temp.getPairedTechs();
					if (pairedTechs[0]._templateName == templateName ||
					    pairedTechs[1]._templateName == templateName)
						return true;
				}
				else if (tech == templateName)
					return true;
			}
		}
		return false;
	}

	/** Find buildings that are capable of researching the given tech */
	findResearchers(templateName, noRequirementCheck)
	{
		// let's check we can research the tech.
		if (!this.canResearch(templateName, noRequirementCheck))
			return undefined;

		const self = this;
		const civ = this.playerData.civ;

		return this.getOwnResearchFacilities().filter(function(ent)
		{
			const techs = ent.researchableTechs(self, civ);
			for (const tech of techs)
			{
				const thisTemp = self.getTemplate(tech);
				if (thisTemp.pairDef())
				{
					const pairedTechs = thisTemp.getPairedTechs();
					if (pairedTechs[0]._templateName == templateName ||
					    pairedTechs[1]._templateName == templateName)
						return true;
				}
				else if (tech == templateName)
					return true;
			}
			return false;
		});
	}

	getEntityLimits()
	{
		return this.playerData.entityLimits;
	}

	getEntityMatchCounts()
	{
		return this.playerData.matchEntityCounts;
	}

	getEntityCounts()
	{
		return this.playerData.entityCounts;
	}

	isTemplateAvailable(templateName)
	{
		if (this.templates[templateName] === undefined)
			this.sharedScript.GetTemplate(templateName);
		return this.templates[templateName] && !this.isTemplateDisabled(templateName);
	}

	isTemplateDisabled(templateName)
	{
		if (!this.playerData.disabledTemplates[templateName])
			return false;
		return this.playerData.disabledTemplates[templateName];
	}

	/** Checks whether the maximum number of buildings have been constructed for a certain catergory */
	isEntityLimitReached(category)
	{
		if (this.playerData.entityLimits[category] === undefined ||
		    this.playerData.entityCounts[category] === undefined)
			return false;
		return this.playerData.entityCounts[category] >= this.playerData.entityLimits[category];
	}

	getTraderTemplatesGains()
	{
		const shipMechantTemplateName = this.applyCiv("units/{civ}/ship_merchant");
		const supportTraderTemplateName = this.applyCiv("units/{civ}/support_trader");
		const shipMerchantTemplate = !this.isTemplateDisabled(shipMechantTemplateName) && this.getTemplate(shipMechantTemplateName);
		const supportTraderTemplate = !this.isTemplateDisabled(supportTraderTemplateName) && this.getTemplate(supportTraderTemplateName);
		const norm = TradeGainNormalization(this.sharedScript.mapSize);
		const ret = {};
		if (supportTraderTemplate)
			ret.landGainMultiplier = norm * supportTraderTemplate.gainMultiplier();
		if (shipMerchantTemplate)
			ret.navalGainMultiplier = norm * shipMerchantTemplate.gainMultiplier();
		return ret;
	}
}
