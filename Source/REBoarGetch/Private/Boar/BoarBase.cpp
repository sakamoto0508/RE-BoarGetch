// Fill out your copyright notice in the Description page of Project Settings.


#include "Boar/BoarBase.h"
#include "Component/CaptureComponent.h"

ABoarBase::ABoarBase()
{
	PrimaryActorTick.bCanEverTick = false;
	CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
}

void ABoarBase::BeginPlay()
{
	Super::BeginPlay();
}

void ABoarBase::Capture()
{
	if (CaptureComponent == nullptr) return;
	CaptureComponent->Capture(nullptr);
}

bool ABoarBase::IsCaptured() const
{
	return CaptureComponent != nullptr && CaptureComponent->IsCaptured();
}
