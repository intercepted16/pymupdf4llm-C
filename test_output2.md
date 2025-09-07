ACME INC 







m 


APS/4HANAEnterpr 




MelbourneSeconda 



OperatingSystem: SUSELinuxEnterpriseServer15SP4 

Servers: 8applicationservers,4databaseservers\(HANASystemReplication\) 



years\(financialcomp 





tem 

n: In-storetransactio 






ements\(NISTCP-10 


s 


Location: SydneyPrimary\+MelbourneSecondary\(loadbalanced\) 

StoreIntegration: Real-timesynchronizationto450\+storelocations 



ustomerLoyaltyPlat 







stem: WindowsServ 




hod: AlwaysOnAvail 


oud 


APIs: RESTfulserviceswithOAuth2.0authentication 

RecoveryRequirements\(NISTCP-10\): 



us: Customerdatap 



sk 





neyPrimary\+Melbo 




ourlyconsistencych 



\) 



BackupMethod: Galeraclusterreplication\+incrementalbackupstoAWSS3 






crosoftSQLServer2 



ion 



Critical-Regulatory 



ns 

lures 


Retention: 7years\(financialreports\),permanent\(annualreports,audit\) 

Compliance: ASXreportingrequirements,ASICregulatoryobligations 



pplyChainOrchestra 





cesInformationSys 



WorkAct,superannu 



Function: Customersupport,casemanagement,knowledgebase 



ployees,450\+store 



Server\+PowerBIPre 




A.2.7SecurityandSurveillanceSystems\(APP-012\) 

System: MilestoneXProtect\+GenetecSecurityCenter 



nagementSystem\(A 


ms 





IGA\+Archibus 




System: VerizonConnectFleetManagement 

Function: Vehicletracking,maintenancescheduling,drivermanagement 



ServicePortal\(APP 





ityManagement\+T 




APPENDIX B: NETWORK TOPOLOGY DIAGRAMS \(NIST SP 800-53 
SECURITY ARCHITECTURE\) 














          




 AES-256 AES-256   AES AES 




MelbourneDC MelbourneDC 



ACMEINCDISASTERRECOVERYNETWORKTOPOLOGY ACMEINCDISASTERRECOVERYNETWORKTOPOLOGY 
\(NISTSP800-34GeographicDistribution\) \(NISTSP800-34GeographicDistribution\) 



\(NISTCP-9\) \(NISTCP-9\) 







 





calBackup  




AICHATBOTBCP/DRTOOLKITINTEGRATION AICHATBOTBCP/DRTOOLKITINTEGRATION 












        Base\) Base\)         
          




k AC-2,AU-2 


g 


rimary Contacts \( 


Role Name 

DRManager Sarah 


Mob 

\+61 



mail Backup 

chen@acme.com.au 


e 


llDRcoordination,ven 




011 

\+61 



MarkPeterson 


Validation/Testing 




C.2 Executive Escalation Chain \(Category 1 Incidents\) 

C.2.1ImmediateEscalation\(Within15minutes\) 


\+61404XXX020 


rity-relatedDRevents 


Mobile 

ms \(On-Call Rotati 


ificationType 



A 



ke<br>\+61404XXX04 


Oracle/NoSQL 


Week PrimarySysA 

Week 
DanielGarcia< 



X 

Linux/Cloud 


econdarySysAdmin Specialty 

ophieMitchell<br>\+61404XXX 



br>\+61404XXX052 









Function Manage 

Finance Financia 



mail DRImpact 

nance@acme.com.au 


tor 071 


Procurement 



Thompson 



hours 




egration 





APPENDIX D: VENDOR CONTACT INFORMATION \(NIST SA-9 



MSRC \+1425 



Immediate 



Engineer: LisaChan 



icrosoft.com 




ServiceLevel 


hone 




mail 













DRHardwareSpecialist: AnthonyRodriguez-\+61404XXX122-a.rodriguez@dell.com 

NISTCompliance: ISO27001,SOC2TypeII 





dors \(Mission-Crit 









D.2.2SAPAustralia 


In-MemoryDB 


om 30minutes 


SophieMitchell-\+6 


om 







\+ 


Service Con 

BotFramework Dev 



mail Availability 


50 



SecurityTeam \+ 

ContactType 


24x7 

ResponseTime 







APPENDIX E: NIST CONTROLS MAPPING MATRIX 



D 


Co 
ControlFamily 
ID 


CME 
mplemen 






U-2 EventLoggin 


Dailyloganalysis 


AuditLog 

Protectionof 


n Monthlycapacity 



n Quarterlyintegrit 



D 


Co 
ControlFamily 
ID 


CME 
mplemen 





P-9 



SystemBacku 


verification 


Func 



ategory Sub 


ACME 
mpleme 



esponsible 
arty 





updates 

verification 




r, 


Technology 



Func 



ategory Sub 


ACME 
mpleme 



R 


esponsible 
arty 



Analysis 


Quarterlytableto 
r 



reviews 

reviews 





Control ControlT 

Informatio 
A.5.2 



atintelligencereports 


ACMEImplementation EvidenceRequired 

DefinedDRteamrolesand Roledocumentation, 


e 

gence 




yptionverification 



orts 


s 


s 


AP 

AP 



rinciple 

Openandtranspar 



Collectionnotices 


CMEImplementation ValidationMethod 

ustomerdatabackup Privacypolicy 




Qualityassurance 


sonalinformation 



personalinformation 

bled Recovery Pr 



kit\) 




forCustomerLoyalt 


n 


"WalkmethroughOraclePOSrecoverysteps"-Step-by-stepguidance 

"Who'stheon-callDBAforemergencyescalation?"-Contactinformation 



Auto-generationofi 


ion: "InitiateCatego 


e\]" 


"Whatsystemsarea 



RecoveryTeamAssembly 
AIChatbot:"AssembleDRteamfor\[SystemName\]recovery" 


required 



lidation 


allaffectedusers 



erveravailability 


Pre-RecoveryAssessment\(5minutes\) 



ANAdatabaserecov 



applicationserverre 






ValidateintegrationwithPOSsystems 

ExpectedRecoveryTimeline: 2hours\(withinRTO\) SuccessCriteria: Allcriticalbusinessprocesses 



cash-onlyprocedur 



er\(NISTCP-7\) 



\(NISTCP-10\(6\)\) 




gionalstoremanager 


ReturntoService\(10minutes\) 

StoreOperationsCoordination 



rityRecovery\(NIST 



eyintegrity 



QLServerfailoverpro 




ComplianceValidation\(30minutes\) 

PrivacyandSecurityVerification 


trolsandauditloggin 

nfrastructureRecov 



erandESXihoststat 



ntVMRecovery 




UpdateVMconfigurationandtools 
Resumenormalbackupschedules 







Metric 

RecoveryTime\(RTO\) 



urrentStatus Trend 

Progress:45min  OnTrack 


Minimize 


  Monitor 

e 


99% 


e 


ation\(NISTCP-10\(6 

mancevalidated 



g 

nvalidated 


IntegrationTesting 
Allsysteminterfacesoperational 





8,000words 



Classification: InternalUseOnly 

AI Chatbot Integration Notes 


gration: Automated 

NIST PM-1 Compl 






5 ITPolicyTeam I 




er\(HelenMitchell\) 


ChiefInformationOfficer\(RobertTaylor\) 
ChiefExecutiveOfficer\(SarahJohnson\) 



