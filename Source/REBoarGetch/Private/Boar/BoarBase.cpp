// Fill out your copyright notice in the Description page of Project Settings.


#include "Boar/BoarBase.h"
#include "GameFramework/CharacterMovementComponent.h"

ABoarBase::ABoarBase()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABoarBase::BeginPlay()
{
	Super::BeginPlay();
}

void ABoarBase::Capture()
{
	if (bIsCaptured)	return;
	
	// 捕獲状態にして、物理を止めます。
	bIsCaptured = true;
	GetCharacterMovement()->DisableMovement();
	GetCharacterMovement()->StopMovementImmediately();
}

