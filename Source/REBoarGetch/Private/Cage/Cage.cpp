// Fill out your copyright notice in the Description page of Project Settings.


#include "Cage/Cage.h"
#include "Boar/BoarBase.h"

// Sets default values
ACage::ACage()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACage::CollectBoar(ABoarBase* Boar)
{
	if (Boar == nullptr)	return;
	
	CapturedBoars.Add(Boar);
	Boar->SetActorLocation(GetActorLocation());
}

