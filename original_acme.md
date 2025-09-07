# **ACME INC**

## **BACKUP POLICY APPENDICES AND REFERENCE DOCUMENTS**

**Document Control**


**Document Title:** Backup Policy Appendices and Reference Documents (NIST Framework


Aligned)


**Document Number:** ACME-IT-POL-001-APP


**Version:** 2.0


**Effective Date:** January 2025


**Classification:** Internal Use Only


**Parent Document:** ACME-IT-POL-001 Data Backup and Recovery Policy

## **APPENDIX A: CRITICAL APPLICATION INVENTORY (NIST SP 800-60** **CATEGORIZED)**

### **A.1 Mission-Essential Functions (Tier 1) - HIGH Impact Systems**


**A.1.1 SAP ERP System**


**Application ID:** APP-001


**System Name:** SAP S/4HANA Enterprise Resource Planning


**NIST Categorization:** HIGH/HIGH/HIGH (Confidentiality/Integrity/Availability)


**Business Function:** Financial management, procurement, inventory control


**Technical Specifications:**


**Database:** SAP HANA 2.0 (12TB primary database)


**Operating System:** SUSE Linux Enterprise Server 15 SP4


**Servers:** 8 application servers, 4 database servers (HANA System Replication)


**Location:** Sydney Primary Data Center (Tier III)


**DR Location:** Melbourne Secondary Data Center (Tier III)


**Cloud Backup:** Azure Australia East region


**Recovery Requirements (NIST CP-10):**


**RTO:** 2 hours | **RPO:** 15 minutes


**Backup Method:** HANA System Replication + hourly snapshots with NIST CP-9 compliance


**Retention:** 7 years (financial compliance), 3 years (operational data)


**Validation:** Daily automated backup verification (NIST CP-9(7))


**Dependencies:** Inventory Management, Customer Loyalty Platform


**Business Impact:** Critical - All financial operations cease, regulatory compliance at risk


**DR Manager Escalation:** Category 1 incident - immediate CIO notification required


**A.1.2 Oracle POS System**


**Application ID:** APP-002


**System Name:** Oracle Retail Point-of-Service (ORPOS)


**NIST Categorization:** HIGH/HIGH/HIGH (Customer transaction processing)


**Business Function:** In-store transaction processing across 450+ retail locations


**Technical Specifications:**


**Database:** Oracle Database 19c RAC (8TB)


**Operating System:** Oracle Linux 8.6


**Servers:** 12 application servers, 6 database nodes (Active-Active cluster)


**Location:** Sydney Primary + Melbourne Secondary (load balanced)


**Store Integration:** Real-time synchronization to 450+ store locations


**Network:** Dedicated MPLS with 10Gbps inter-site connectivity


**Recovery Requirements (NIST CP-10):**

**RTO:** 1 hour | **RPO:** 5 minutes
**Backup Method:** Oracle Data Guard + continuous log shipping
**Retention:** 7 years (transaction records for audit compliance)
**Validation:** Real-time replication monitoring with automated failover testing
**Dependencies:** Customer Loyalty Platform, Financial Reporting System
**Business Impact:** Critical - Immediate revenue loss, customer service disruption
**DR Manager Notes:** Highest priority for recovery - affects all 450+ stores simultaneously
**A.1.3 Customer Loyalty Platform**

**Application ID:** APP-003
**System Name:** ACME Rewards Customer Management System
**NIST Categorization:** HIGH/HIGH/MODERATE (Customer personal data)
**Business Function:** Customer loyalty program, personalization, promotional campaigns
**Technical Specifications:**

**Database:** Microsoft SQL Server 2022 Always On Availability Groups (4TB)
**Operating System:** Windows Server 2022 Datacenter
**Servers:** 6 application servers, 4 database servers (Always On cluster)
**Location:** Sydney Primary Data Center with Melbourne replica
**Integration:** Mobile app (iOS/Android), POS systems, marketing automation
**APIs:** RESTful services with OAuth 2.0 authentication
**Recovery Requirements (NIST CP-10):**

**RTO:** 2 hours | **RPO:** 15 minutes
**Backup Method:** Always On Availability Groups + log shipping to cloud
**Retention:** 5 years (customer analytics), permanent (legal holds)
**Privacy Compliance:** Australian Privacy Act, APP 11 security requirements
**Dependencies:** POS System, Marketing Automation Platform
**Business Impact:** High - Customer experience degradation, lost personalization data
**DR Manager Focus:** Customer data protection priority, privacy breach risk
**A.1.4 Inventory Management System**

**Application ID:** APP-004
**System Name:** ACME Intelligent Inventory Management (AIIM)
**NIST Categorization:** HIGH/HIGH/HIGH (Supply chain critical)
**Business Function:** Stock control, automated ordering, supply chain optimization
**Technical Specifications:**

**Database:** MySQL 8.0 with Galera Cluster (6TB)
**Operating System:** Ubuntu Server 22.04 LTS
**Servers:** 10 application servers, 6 database servers (multi-master replication)
**Location:** Sydney Primary + Melbourne Secondary (active-active)
**Integration:** SAP ERP, 200+ supplier systems, IoT sensors (12,000+ devices)
**Analytics:** Real-time inventory optimization with machine learning
**Recovery Requirements (NIST CP-10):**

**RTO:** 2 hours | **RPO:** 15 minutes
**Backup Method:** Galera cluster replication + incremental backups to AWS S3
**Retention:** 3 years (inventory movements), 7 years (financial impacts)
**Validation:** Hourly consistency checks across cluster nodes
**Dependencies:** SAP ERP, Supplier portals, IoT sensor network
**Business Impact:** Critical - Stock-outs, over-ordering, $2M+ daily supply chain impact
**DR Manager Priority:** Supply chain disruption affects 450+ stores within 24 hours
**A.1.5 Financial Reporting System**

**Application ID:** APP-005
**System Name:** ACME Financial Intelligence Platform (AFIP)
**NIST Categorization:** HIGH/HIGH/MODERATE (Financial reporting & compliance)
**Business Function:** Financial reporting, regulatory compliance, business intelligence
**Technical Specifications:**

**Database:** Microsoft SQL Server 2022 with Analysis Services (5TB)
**Operating System:** Windows Server 2022 Standard
**Servers:** 4 application servers, 4 database servers, 2 SSAS cubes
**Location:** Sydney Primary Data Center (secure financial zone)
**Integration:** SAP ERP, external audit systems (Big 4 accounting firm)
**Reporting:** Power BI Premium, SSRS, regulatory submission automation
**Recovery Requirements (NIST CP-10):**

**RTO:** 2 hours | **RPO:** 15 minutes
**Backup Method:** Always On Availability Groups + cube backups + cloud archive
**Retention:** 7 years (financial reports), permanent (annual reports, audit)
**Compliance:** ASX reporting requirements, ASIC regulatory obligations
**Dependencies:** SAP ERP, external audit systems, regulatory reporting APIs
**Business Impact:** Critical - Regulatory non-compliance, ASX reporting failures
**DR Manager Notes:** Regulatory timeline compliance critical - ASIC penalties apply### **A.2 Primary Business Functions (Tier 2) - MODERATE Impact Systems**

**A.2.1 Supply Chain Management (APP-006)**

**System:** ACME Supply Chain Orchestrator (ASCO)
**NIST Categorization:** MODERATE/HIGH/MODERATE
**Function:** Supplier management, logistics coordination, delivery scheduling
**Recovery:** RTO 4 hours / RPO 1 hour
**Critical Dependencies:** 200+ supplier integrations, logistics partners
**A.2.2 Human Resources Information System (APP-007)**

**System:** Workday HCM Enterprise (SaaS)
**NIST Categorization:** HIGH/HIGH/MODERATE (Employee PII)
**Function:** Employee management, payroll (5,000+ employees), performance tracking
**Recovery:** RTO 4 hours / RPO 4 hours
**Compliance:** Fair Work Act, superannuation obligations
**A.2.3 Customer Service Portal (APP-008)**

**System:** Salesforce Service Cloud
**NIST Categorization:** MODERATE/HIGH/MODERATE
**Function:** Customer support, case management, knowledge base
**Recovery:** RTO 8 hours / RPO 4 hours
**Integration:** POS systems, Customer Loyalty Platform
**A.2.4 Email and Collaboration Platform (APP-009)**

**System:** Microsoft 365 Enterprise E5
**NIST Categorization:** MODERATE/MODERATE/MODERATE
**Function:** Email, SharePoint, Teams, OneDrive
**Recovery:** RTO 8 hours / RPO 4 hours
**Users:** 5,000+ employees, 450+ store locations
**A.2.5 Marketing Automation Platform (APP-010)**

**System:** Adobe Campaign + Marketo Engage
**NIST Categorization:** MODERATE/MODERATE/LOW
**Function:** Email marketing, campaign management, customer segmentation
**Recovery:** RTO 8 hours / RPO 4 hours
**Integration:** Customer Loyalty Platform, analytics systems
**A.2.6 Business Intelligence and Analytics (APP-011)**

**System:** Tableau Server + Power BI Premium
**NIST Categorization:** MODERATE/HIGH/MODERATE
**Function:** Data visualization, executive dashboards, self-service analytics
**Recovery:** RTO 8 hours / RPO 4 hours
**Data Sources:** All Tier 1 systems, external market data
**A.2.7 Security and Surveillance Systems (APP-012)**

**System:** Milestone XProtect + Genetec Security Center
**NIST Categorization:** MODERATE/HIGH/MODERATE
**Function:** Video surveillance (450+ stores), access control, incident management
**Recovery:** RTO 8 hours / RPO 4 hours
**Storage:** 90-day retention, 24/7 monitoring### **A.3 Supporting Business Functions (Tier 3) - LOW Impact Systems**

**A.3.1 Document Management System (APP-013)**

**System:** SharePoint Server 2022 + Microsoft Purview
**Function:** Document storage, version control, workflow management
**Recovery:** RTO 24 hours / RPO 24 hours
**A.3.2 Training and Learning Management (APP-014)**

**System:** Cornerstone OnDemand LMS
**Function:** Employee training, compliance tracking, certification management
**Recovery:** RTO 24 hours / RPO 24 hours
**A.3.3 Facility Management System (APP-015)**

**System:** IBM TRIRIGA + Archibus
**Function:** Space management, maintenance scheduling, energy monitoring
**Recovery:** RTO 24 hours / RPO 24 hours
**A.3.4 Vehicle Fleet Management (APP-016)**

**System:** Verizon Connect Fleet Management
**Function:** Vehicle tracking, maintenance scheduling, driver management
**Recovery:** RTO 24 hours / RPO 24 hours
**A.3.5 Energy Management System (APP-017)**

**System:** Schneider Electric EcoStruxure
**Function:** Energy monitoring, sustainability reporting, cost optimization
**Recovery:** RTO 24 hours / RPO 24 hours
**A.3.6 Employee Self-Service Portal (APP-018)**

**System:** Custom .NET application + SQL Server
**Function:** Employee portal, time tracking, benefits management
**Recovery:** RTO 24 hours / RPO 24 hours
**A.3.7 Vendor Portal and Procurement (APP-019)**

**System:** SAP Ariba + Oracle Supplier Network
**Function:** Supplier onboarding, procurement processes, contract management
**Recovery:** RTO 8 hours / RPO 4 hours
**A.3.8 Quality Management System (APP-020)**

**System:** SAP Quality Management + TrackWise
**Function:** Quality control, compliance tracking, audit management
**Recovery:** RTO 24 hours / RPO 24 hours## **APPENDIX B: NETWORK TOPOLOGY DIAGRAMS (NIST SP 800-53** **SECURITY ARCHITECTURE)**
### **B.1 NIST Cybersecurity Framework Backup Network Architecture**
ACME INC SYDNEY PRIMARY DATA CENTERACME INC SYDNEY PRIMARY DATA CENTER(NIST CSF 2.0 Aligned Architecture)(NIST CSF 2.0 Aligned Architecture)
**┌─────────────────────────────────────────────────────────────────┐**

**│** DMZ Zone (NIST SC-7)                       DMZ Zone (NIST SC-7) **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** Backup     SIEM/SOC     AI Chatbot **│** Backup **│** **│** SIEM/SOC **│** **│** AI Chatbot **│** **│**

**│** Proxy      Platform     BCP/DR **│** Proxy **│** **│** Platform **│** **│** BCP/DR **│** **│**

**│** (Veeam B&R)   (Splunk)     Assistant **│** (Veeam B&R) **│** **│** (Splunk) **│** **│** Assistant **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**│** **│** **│** **│** **│**

**│** NIST CP-9      NIST SI-4      NIST AT-2        NIST CP-9      NIST SI-4      NIST AT-2 **│**

**└─────────────────────────────────────────────────────────────────┘**

**│** **│** **│**

**┌─────────────────────────────────────────────────────────────────┐**

**│** Production Zone (NIST AC-4)                   Production Zone (NIST AC-4) **│**

**│** **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** SAP ERP     Oracle POS    Customer **│** SAP ERP **│** **│** Oracle POS **│** **│** Customer **│** **│**

**│** HANA Cluster   RAC Cluster   Loyalty **│** HANA Cluster **│** **│** RAC Cluster **│** **│** Loyalty **│** **│**

**│** (Encrypted)   (Encrypted)   Platform **│** (Encrypted) **│** **│** (Encrypted) **│** **│** Platform **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**│** **│** **│** **│** **│**

**│** NIST SC-13     NIST SC-13      NIST SC-13       NIST SC-13     NIST SC-13      NIST SC-13 **│**

**│** **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** Inventory    Financial    Supply Chain **│** Inventory **│** **│** Financial **│** **│** Supply Chain **│** **│**

**│** Management    Reporting    Management **│** Management **│** **│** Reporting **│** **│** Management **│** **│**

**│** (MySQL)     (SQL Server)   (Oracle) **│** (MySQL) **│** **│** (SQL Server) **│** **│** (Oracle) **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**└─────────────────────────────────────────────────────────────────┘**

**│** **│** **│**

**┌─────────────────────────────────────────────────────────────────┐**

**│** Storage Zone (NIST MP-4)                     Storage Zone (NIST MP-4) **│**

**│** **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** Dell EMC     Pure Storage   Azure Blob **│** Dell EMC **│** **│** Pure Storage **│** **│** Azure Blob **│** **│**

**│** PowerMax     FlashArray    Storage **│** PowerMax **│** **│** FlashArray **│** **│** Storage **│** **│**

**│** (Primary)    (Secondary)    (Archive) **│** (Primary) **│** **│** (Secondary) **│** **│** (Archive) **│** **│**

**│** AES-256     AES-256      AES-256 **│** AES-256 **│** **│** AES-256 **│** **│** AES-256 **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**└─────────────────────────────────────────────────────────────────┘**
### **B.2 Multi-Site Disaster Recovery Network (NIST CP-6/CP-7)**
ACME INC DISASTER RECOVERY NETWORK TOPOLOGYACME INC DISASTER RECOVERY NETWORK TOPOLOGY(NIST SP 800-34 Geographic Distribution)(NIST SP 800-34 Geographic Distribution)
**┌─────────────────┐** **┌─────────────────┐** **┌─────────────────┐**

**│** Sydney DC      Melbourne DC     Cloud DR Sites  Sydney DC **│** **│** Melbourne DC **│** **│** Cloud DR Sites **│**

**│** (Primary)      (Secondary)     (Tertiary)    (Primary) **│** **│** (Secondary) **│** **│** (Tertiary) **│**

**│** NIST Tier III    NIST Tier III    CSP Tier IV   NIST Tier III **│** **│** NIST Tier III **│** **│** CSP Tier IV **│**

**│** **│** **│** **│** **│** **│**

**│** **┌─────────────┐** **│** **│** **┌─────────────┐** **│** **│** **┌─────────────┐** **│**

**│** Veeam B&R  ◄► Veeam B&R     Azure Site **│** Veeam B&R **│**  - **┼────┼**  - **│** Veeam B&R **│** **│** **│** **│** Azure Site **│** **│**

**│** Primary      Secondary     Recovery **│** Primary **│** **│** **│** **│** Secondary **│** **│** **│** **│** Recovery **│** **│**

**│** (NIST CP-9)    (NIST CP-9)    (NIST CP-10) **│** (NIST CP-9) **│** **│** **│** **│** (NIST CP-9) **│** **│** **│** **│** (NIST CP-10) **│** **│**

**│** **└─────────────┘** **│** **│** **└─────────────┘** **│** **│** **└─────────────┘** **│**

**│** **│** **│** **│** **│** **│**

**│** **┌─────────────┐** **│** **│** **┌─────────────┐** **│** **│** **┌─────────────┐** **│**

**│** Dell EMC      Dell EMC      AWS S3 **│** Dell EMC **│** **│** **│** **│** Dell EMC **│** **│** **│** **│** AWS S3 **│** **│**

**│** PowerProtect◄► PowerProtect    Deep Archive **│** PowerProtect **│**  - **┼────┼**  - **│** PowerProtect **│** **│** **│** **│** Deep Archive **│** **│**

**│** (NIST MP-4)    (NIST MP-4)    (NIST MP-4) **│** (NIST MP-4) **│** **│** **│** **│** (NIST MP-4) **│** **│** **│** **│** (NIST MP-4) **│** **│**

**│** **└─────────────┘** **│** **│** **└─────────────┘** **│** **│** **└─────────────┘** **│**

**│** **│** **│** **│** **│** **│**

**│** DR Manager      DR Manager      Cloud DR     DR Manager **│** **│** DR Manager **│** **│** Cloud DR **│**

**│** Sarah Chen    Sarah Chen      Backup Team     Automation **│** **│** Backup Team **│** **│** Automation **│**

**│** Primary Contact   Secondary Site    NIST CP-10    Primary Contact **│** **│** Secondary Site **│** **│** NIST CP-10 **│**

**│** **│** **│** **│** **│** **│**

**│** Network: 10Gbps   Network: 10Gbps   Network: 1Gbps  Network: 10Gbps **│** **│** Network: 10Gbps **│** **│** Network: 1Gbps **│**

**│** Latency: <1ms    Latency: 12ms    Latency: 25ms  Latency: <1ms **│** **│** Latency: 12ms **│** **│** Latency: 25ms **│**

**│** Encryption: Yes   Encryption: Yes   Encryption: Yes Encryption: Yes **│** **│** Encryption: Yes **│** **│** Encryption: Yes **│**

**└─────────────────┘** **└─────────────────┘** **└─────────────────┘**

**│** **│** **│**

**└───────────────────────┼───────────────────────┘**

**│**

**┌─────────────────┐**

**│** Store Network  Store Network **│**

**│** (450+ Stores)  (450+ Stores) **│**

**│** NIST SC-7    NIST SC-7 **│**

**│** **│**

**│** **┌─────────────┐** **│**

**│** Local Backup **│** Local Backup **│** **│**

**│** Appliances **│** Appliances **│** **│**

**│** (24-hour) **│** (24-hour) **│** **│**

**│** NIST CP-9 **│** NIST CP-9 **│** **│**

**│** **└─────────────┘** **│**

**└─────────────────┘**
### **B.3 AI Chatbot Integration Architecture for DR Manager**
AI CHATBOT BCP/DR TOOLKIT INTEGRATIONAI CHATBOT BCP/DR TOOLKIT INTEGRATION(Sarah Chen's DR Management Dashboard)(Sarah Chen's DR Management Dashboard)
**┌─────────────────────────────────────────────────────────────────┐**

**│** AI Chatbot Interface                       AI Chatbot Interface **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** Voice/Chat    Mobile App    Web Portal **│** Voice/Chat **│** **│** Mobile App **│** **│** Web Portal **│** **│**

**│** Interface    iOS/Android   Dashboard **│** Interface **│** **│** iOS/Android **│** **│** Dashboard **│** **│**

**│** (Teams Bot)   (Field Use)   (Desktop) **│** (Teams Bot) **│** **│** (Field Use) **│** **│** (Desktop) **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**└─────────────────────────────────────────────────────────────────┘**

**│** **│** **│**

**┌─────────────────────────────────────────────────────────────────┐**

**│** Integration Layer                        Integration Layer **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** ServiceNow    Microsoft    Monitoring **│** ServiceNow **│** **│** Microsoft **│** **│** Monitoring **│** **│**

**│** ITSM       Teams      Systems **│** ITSM **│** **│** Teams **│** **│** Systems **│** **│**

**│** Integration   Integration   (SCOM/SIEM) **│** Integration **│** **│** Integration **│** **│** (SCOM/SIEM) **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**└─────────────────────────────────────────────────────────────────┘**

**│** **│** **│**

**┌─────────────────────────────────────────────────────────────────┐**

**│** Data Sources                           Data Sources **│**

**│** **┌─────────────┐** **┌─────────────┐** **┌─────────────┐** **│**

**│** Backup      Recovery     Contact **│** Backup **│** **│** Recovery **│** **│** Contact **│** **│**

**│** Status APIs   Procedures    Directory **│** Status APIs **│** **│** Procedures **│** **│** Directory **│** **│**

**│** (Real-time)   (Knowledge    (Dynamic) **│** (Real-time) **│** **│** (Knowledge **│** **│** (Dynamic) **│** **│**

**│** Base) **│** **│** **│** Base) **│** **│** **│** **│**

**│** **└─────────────┘** **└─────────────┘** **└─────────────┘** **│**

**└─────────────────────────────────────────────────────────────────┘**
### **B.4 Network Security Controls (NIST SP 800-53)**
|Network Segment|NIST Controls|Security Measures||---|---|---||**DMZ Zone**|SC-7, AC-4|Firewall rules, IPS, network segmentation||**Production Zone**|SC-7, SC-8|Encrypted communication, access controls||**Storage Zone**|MP-4, SC-13|Encryption at rest, secure key management||**Management Network**|AC-2, AU-2|MFA required, full audit logging||<br><br>**WAN Connections**<br>SC-8, SC-12<br>VPN encryption, certificate management|<br><br>**WAN Connections**<br>SC-8, SC-12<br>VPN encryption, certificate management|<br><br>**WAN Connections**<br>SC-8, SC-12<br>VPN encryption, certificate management|## **APPENDIX C: EMERGENCY CONTACT LIST (DR MANAGER FOCUSED)**
### **C.1 DR Manager Primary Contacts (Sarah Chen's Team)**

**C.1.1 DR Manager - Primary Contact**
|Role|Name|Mobile|Email|Backup||---|---|---|---|---||**DR Manager**|**Sarah Chen**|**+61 404**<br>**XXX 001**|**s.chen@acme.com.au**|**IT Operations**<br>**Manager**||Certifications|CBCP, PMP|Location|Sydney HQ|Escalation||Responsibilities|Overall DR coordination, vendor<br>management, executive<br>reporting|||||<br><br>Availability<br>24/7 on-call rotation<br>Backup<br>Phone<br>+61 404 XXX 002|<br><br>Availability<br>24/7 on-call rotation<br>Backup<br>Phone<br>+61 404 XXX 002|<br><br>Availability<br>24/7 on-call rotation<br>Backup<br>Phone<br>+61 404 XXX 002|<br><br>Availability<br>24/7 on-call rotation<br>Backup<br>Phone<br>+61 404 XXX 002|<br><br>Availability<br>24/7 on-call rotation<br>Backup<br>Phone<br>+61 404 XXX 002|
**C.1.2 DR Specialists Team (Sarah's Direct Reports)**
|Role|Name|Mobile|Email|Specialization||---|---|---|---|---||**Senior DR Analyst**|Michael<br>Rodriguez|+61 404 XXX<br>010|m.rodriguez@acme.com.au|Database Recovery||**DR Systems**<br>**Engineer**|Jennifer Walsh|+61 404 XXX<br>011|j.walsh@acme.com.au|Infrastructure/Cloud||**DR Compliance**<br>**Lead**|David Kumar|+61 404 XXX<br>012|d.kumar@acme.com.au|NIST/ISO<br>Compliance||**DR**<br>**Communications**|Lisa Chang|+61 404 XXX<br>013|l.chang@acme.com.au|Stakeholder Comms||**DR Testing Lead**|Mark Peterson|+61 404 XXX<br>014|m.peterson@acme.com.au|Validation/Testing||<br><br>**Store DR**<br>**Coordinator**<br>Rachel Kim<br>+61 404 XXX<br>015<br>r.kim@acme.com.au<br>Retail Operations|<br><br>**Store DR**<br>**Coordinator**<br>Rachel Kim<br>+61 404 XXX<br>015<br>r.kim@acme.com.au<br>Retail Operations|<br><br>**Store DR**<br>**Coordinator**<br>Rachel Kim<br>+61 404 XXX<br>015<br>r.kim@acme.com.au<br>Retail Operations|<br><br>**Store DR**<br>**Coordinator**<br>Rachel Kim<br>+61 404 XXX<br>015<br>r.kim@acme.com.au<br>Retail Operations|<br><br>**Store DR**<br>**Coordinator**<br>Rachel Kim<br>+61 404 XXX<br>015<br>r.kim@acme.com.au<br>Retail Operations|### **C.2 Executive Escalation Chain (Category 1 Incidents)**

**C.2.1 Immediate Escalation (Within 15 minutes)**
|Role|Name|Mobile|Email|Escalation Trigger||---|---|---|---|---||**CISO**|Amanda Foster|+61 404 XXX 020|a.foster@acme.com.au|Security-related DR events||**CIO**|Robert Taylor|+61 404 XXX 021|r.taylor@acme.com.au|All Category 1 incidents||<br><br>**COO**<br>Catherine Brown<br>+61 404 XXX 022<br>c.brown@acme.com.au<br>Store operations impact|<br><br>**COO**<br>Catherine Brown<br>+61 404 XXX 022<br>c.brown@acme.com.au<br>Store operations impact|<br><br>**COO**<br>Catherine Brown<br>+61 404 XXX 022<br>c.brown@acme.com.au<br>Store operations impact|<br><br>**COO**<br>Catherine Brown<br>+61 404 XXX 022<br>c.brown@acme.com.au<br>Store operations impact|<br><br>**COO**<br>Catherine Brown<br>+61 404 XXX 022<br>c.brown@acme.com.au<br>Store operations impact|
**C.2.2 Executive Team (Within 30 minutes)**
|Role|Name|Mobile|Email|Notification Type||---|---|---|---|---||**CEO**|Sarah Johnson|+61 404 XXX 030|s.johnson@acme.com.au|Category 1 incidents||**CFO**|David Williams|+61 404 XXX 031|d.williams@acme.com.au|Financial system impacts||<br><br>**Chief Legal**<br>Michelle Wong<br>+61 404 XXX 032<br>m.wong@acme.com.au<br>Regulatory/privacy breaches|<br><br>**Chief Legal**<br>Michelle Wong<br>+61 404 XXX 032<br>m.wong@acme.com.au<br>Regulatory/privacy breaches|<br><br>**Chief Legal**<br>Michelle Wong<br>+61 404 XXX 032<br>m.wong@acme.com.au<br>Regulatory/privacy breaches|<br><br>**Chief Legal**<br>Michelle Wong<br>+61 404 XXX 032<br>m.wong@acme.com.au<br>Regulatory/privacy breaches|<br><br>**Chief Legal**<br>Michelle Wong<br>+61 404 XXX 032<br>m.wong@acme.com.au<br>Regulatory/privacy breaches|### **C.3 Technical Teams (On-Call Rotation)**

**C.3.1 Database Administration Team**
|Week|Primary DBA|Secondary DBA|Specialty||---|---|---|---||**Week 1**|Tom Anderson<br>+61 404 XXX 040|Jenny Lee<br>+61 404 XXX 041|Oracle/SAP HANA||**Week 2**|Sarah Davis<br>+61 404 XXX 042|Peter Kim<br>+61 404 XXX 043|SQL Server/MySQL||**Week 3**|Andrew Clarke<br>+61 404 XXX 044|Linda Zhang<br>+61 404 XXX 045|Oracle/NoSQL||<br><br>**Week 4**<br>Kevin O'Brien<br>+61 404 XXX 046<br>Emma Johnson<br>+61 404 XXX 047<br>SQL Server/Cloud|<br><br>**Week 4**<br>Kevin O'Brien<br>+61 404 XXX 046<br>Emma Johnson<br>+61 404 XXX 047<br>SQL Server/Cloud|<br><br>**Week 4**<br>Kevin O'Brien<br>+61 404 XXX 046<br>Emma Johnson<br>+61 404 XXX 047<br>SQL Server/Cloud|<br><br>**Week 4**<br>Kevin O'Brien<br>+61 404 XXX 046<br>Emma Johnson<br>+61 404 XXX 047<br>SQL Server/Cloud|
**C.3.2 Infrastructure Team**
|Week|Primary SysAdmin|Secondary SysAdmin|Specialty||---|---|---|---||**Week**<br>**1**|Daniel Garcia<br>+61 404 XXX 050|Sophie Mitchell<br>+61 404 XXX<br>051|VMware/Windows||**Week**<br>**2**|Ryan Clarke<br>+61 404 XXX 052|Alice Chen<br>+61 404 XXX 053|Linux/Cloud||**Week**<br>**3**|Patrick O'Connor<br>+61 404 XXX 054|Steve Morrison<br>+61 404 XXX<br>055|Network/Security||<br><br>**Week**<br>**4**<br>Anthony Rodriguez<br>+61 404 XXX<br>056<br>Helen Mitchell<br>+61 404 XXX<br>057<br>Storage/Backup|<br><br>**Week**<br>**4**<br>Anthony Rodriguez<br>+61 404 XXX<br>056<br>Helen Mitchell<br>+61 404 XXX<br>057<br>Storage/Backup|<br><br>**Week**<br>**4**<br>Anthony Rodriguez<br>+61 404 XXX<br>056<br>Helen Mitchell<br>+61 404 XXX<br>057<br>Storage/Backup|<br><br>**Week**<br>**4**<br>Anthony Rodriguez<br>+61 404 XXX<br>056<br>Helen Mitchell<br>+61 404 XXX<br>057<br>Storage/Backup|### **C.4 Business Stakeholder Contacts**

**C.4.1 Store Operations (450+ Stores)**
|Region|Regional Manager|Mobile|Email|Stores Count||---|---|---|---|---||**NSW/ACT**|Christopher Evans|+61 404 XXX 060|c.evans@acme.com.au|180 stores||**VIC/TAS**|Susan Campbell|+61 404 XXX 061|s.campbell@acme.com.au|145 stores||**QLD/NT**|James Wilson|+61 404 XXX 062|j.wilson@acme.com.au|85 stores||<br><br>**WA/SA**<br>Catherine Brown<br>+61 404 XXX 063<br>c.brown@acme.com.au<br>40 stores|<br><br>**WA/SA**<br>Catherine Brown<br>+61 404 XXX 063<br>c.brown@acme.com.au<br>40 stores|<br><br>**WA/SA**<br>Catherine Brown<br>+61 404 XXX 063<br>c.brown@acme.com.au<br>40 stores|<br><br>**WA/SA**<br>Catherine Brown<br>+61 404 XXX 063<br>c.brown@acme.com.au<br>40 stores|<br><br>**WA/SA**<br>Catherine Brown<br>+61 404 XXX 063<br>c.brown@acme.com.au<br>40 stores|
**C.4.2 Key Business Functions**
|Function|Manager|Mobile|Email|DR Impact||---|---|---|---|---||**Finance**|Financial Controller|+61 404 XXX<br>070|finance@acme.com.au|SAP ERP, Financial<br>Reporting||**Supply Chain**|Supply Chain<br>Director|+61 404 XXX<br>071|supply@acme.com.au|Inventory,<br>Procurement||**Customer**<br>**Service**|Customer Service<br>Manager|+61 404 XXX<br>072|service@acme.com.au|Customer Portal,<br>Loyalty||<br><br>**Marketing**<br>Marketing Director<br>+61 404 XXX<br>073<br>marketing@acme.com.au<br>Marketing Automation|<br><br>**Marketing**<br>Marketing Director<br>+61 404 XXX<br>073<br>marketing@acme.com.au<br>Marketing Automation|<br><br>**Marketing**<br>Marketing Director<br>+61 404 XXX<br>073<br>marketing@acme.com.au<br>Marketing Automation|<br><br>**Marketing**<br>Marketing Director<br>+61 404 XXX<br>073<br>marketing@acme.com.au<br>Marketing Automation|<br><br>**Marketing**<br>Marketing Director<br>+61 404 XXX<br>073<br>marketing@acme.com.au<br>Marketing Automation|### **C.5 AI Chatbot Integration Contacts**

**C.5.1 AI Chatbot Support Team**
|Role|Contact|Phone|Email|Availability||---|---|---|---|---||**AI Solutions Architect**|Dr. Alex<br>Thompson|+61 404 XXX<br>080|a.thompson@acme.com.au|Business<br>hours||**Chatbot**<br>**Administrator**|Priya Sharma|+61 404 XXX<br>081|p.sharma@acme.com.au|24/7 support||<br><br>**Integration Specialist**<br>Marcus Johnson<br>+61 404 XXX<br>082<br>m.johnson@acme.com.au<br>Business<br>hours|<br><br>**Integration Specialist**<br>Marcus Johnson<br>+61 404 XXX<br>082<br>m.johnson@acme.com.au<br>Business<br>hours|<br><br>**Integration Specialist**<br>Marcus Johnson<br>+61 404 XXX<br>082<br>m.johnson@acme.com.au<br>Business<br>hours|<br><br>**Integration Specialist**<br>Marcus Johnson<br>+61 404 XXX<br>082<br>m.johnson@acme.com.au<br>Business<br>hours|<br><br>**Integration Specialist**<br>Marcus Johnson<br>+61 404 XXX<br>082<br>m.johnson@acme.com.au<br>Business<br>hours|
**C.5.2 ServiceNow Integration**
|Role|Contact|Phone|Email|Specialty||---|---|---|---|---||**ServiceNow Admin**|Jennifer Walsh|+61 404 XXX 090|j.walsh@acme.com.au|ITSM Integration||<br><br>**Workflow Designer**<br>David Kumar<br>+61 404 XXX 091<br>d.kumar@acme.com.au<br>Automation|<br><br>**Workflow Designer**<br>David Kumar<br>+61 404 XXX 091<br>d.kumar@acme.com.au<br>Automation|<br><br>**Workflow Designer**<br>David Kumar<br>+61 404 XXX 091<br>d.kumar@acme.com.au<br>Automation|<br><br>**Workflow Designer**<br>David Kumar<br>+61 404 XXX 091<br>d.kumar@acme.com.au<br>Automation|<br><br>**Workflow Designer**<br>David Kumar<br>+61 404 XXX 091<br>d.kumar@acme.com.au<br>Automation|## **APPENDIX D: VENDOR CONTACT INFORMATION (NIST SA-9** **COMPLIANT)**
### **D.1 Primary Technology Vendors (24/7 Support)**

**D.1.1 Microsoft Corporation (Azure, Office 365)**
|Service Level|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**Premier Support**|Enterprise|+61 1800 197 503|premier@microsoft.com|1 hour||**Azure Critical**|Priority 1|+61 1800 197 503|azuresupport@microsoft.com|15 minutes||<br><br>**Security Response**<br>MSRC<br>+1 425 882 8080<br>secure@microsoft.com<br>Immediate|<br><br>**Security Response**<br>MSRC<br>+1 425 882 8080<br>secure@microsoft.com<br>Immediate|<br><br>**Security Response**<br>MSRC<br>+1 425 882 8080<br>secure@microsoft.com<br>Immediate|<br><br>**Security Response**<br>MSRC<br>+1 425 882 8080<br>secure@microsoft.com<br>Immediate|<br><br>**Security Response**<br>MSRC<br>+1 425 882 8080<br>secure@microsoft.com<br>Immediate|
**Sarah Chen's Account Team:**

**Customer Success Manager:** [Jennifer Walsh - +61 404 XXX 100 - j.walsh@microsoft.com](mailto:j.walsh@microsoft.com)
**Technical Account Manager:** [David Kumar - +61 404 XXX 101 - d.kumar@microsoft.com](mailto:d.kumar@microsoft.com)
**Premier Support Engineer:** [Lisa Chang - +61 404 XXX 102 - l.chang@microsoft.com](mailto:l.chang@microsoft.com)
**DR Specialist:** [Mark Peterson - +61 404 XXX 103 - m.peterson@microsoft.com](mailto:m.peterson@microsoft.com)
**NIST Compliance:** Microsoft SOC 2 Type II, FedRAMP High, ISO 27001
**D.1.2 Amazon Web Services (AWS)**
|Service Level|Contact Type|Phone|Email|Response<br>Time||---|---|---|---|---||**Enterprise**<br>**Support**|Technical Account<br>Manager|+61 1800 751<br>575|enterprise@aws.com|15 minutes||**Business**<br>**Support**|General Support|+61 1800 751<br>575|aws-<br>support@amazon.com|1 hour||<br><br>**Security Team**<br>AWS Security<br>+1 206 266<br>4064<br>aws-<br>security@amazon.com<br>15 minutes|<br><br>**Security Team**<br>AWS Security<br>+1 206 266<br>4064<br>aws-<br>security@amazon.com<br>15 minutes|<br><br>**Security Team**<br>AWS Security<br>+1 206 266<br>4064<br>aws-<br>security@amazon.com<br>15 minutes|<br><br>**Security Team**<br>AWS Security<br>+1 206 266<br>4064<br>aws-<br>security@amazon.com<br>15 minutes|<br><br>**Security Team**<br>AWS Security<br>+1 206 266<br>4064<br>aws-<br>security@amazon.com<br>15 minutes|
**Account Team:**

**Technical Account Manager:** [Rachel Kim - +61 404 XXX 110 - r.kim@amazon.com](mailto:r.kim@amazon.com)
**Solutions Architect:** [Steve Morrison - +61 404 XXX 111 - s.morrison@amazon.com](mailto:s.morrison@amazon.com)
**DR Consultant:** [Alice Chen - +61 404 XXX 112 - a.chen@amazon.com](mailto:a.chen@amazon.com)
**NIST Compliance:** AWS FedRAMP High, SOC 1/2/3, ISO 27001
**D.1.3 Dell Technologies (Infrastructure)**
|Service Level|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**ProSupport Plus**|Critical Hardware|1800 624 253|prosupport@dell.com|4-hour onsite||**Premium Support**|Software/Firmware|1800 624 253|premium@dell.com|1 hour||<br><br>**Mission Critical**<br>Emergency Response<br>1800 624 253<br>mission-critical@dell.com<br>2-hour onsite|<br><br>**Mission Critical**<br>Emergency Response<br>1800 624 253<br>mission-critical@dell.com<br>2-hour onsite|<br><br>**Mission Critical**<br>Emergency Response<br>1800 624 253<br>mission-critical@dell.com<br>2-hour onsite|<br><br>**Mission Critical**<br>Emergency Response<br>1800 624 253<br>mission-critical@dell.com<br>2-hour onsite|<br><br>**Mission Critical**<br>Emergency Response<br>1800 624 253<br>mission-critical@dell.com<br>2-hour onsite|
**Account Team for Sarah Chen:**

**Enterprise Account Executive:** [Patrick O'Connor - +61 404 XXX 120 - p.oconnor@dell.com](mailto:p.oconnor@dell.com)
**Technical Account Manager:** [Sophie Mitchell - +61 404 XXX 121 - s.mitchell@dell.com](mailto:s.mitchell@dell.com)
**DR Hardware Specialist:** [Anthony Rodriguez - +61 404 XXX 122 - a.rodriguez@dell.com](mailto:a.rodriguez@dell.com)
**NIST Compliance:** ISO 27001, SOC 2 Type II
**D.1.4 Veeam Software (Backup & Replication)**
|Service Level|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**Premier Support**|24x7 Technical|+61 1800 441 953|support@veeam.com|1 hour||**Emergency Support**|Critical Issues|+61 1800 441 953|emergency@veeam.com|30 minutes||<br><br>**Professional Services**<br>DR Consulting<br>+61 2 8218 2550<br>services@veeam.com<br>4 hours|<br><br>**Professional Services**<br>DR Consulting<br>+61 2 8218 2550<br>services@veeam.com<br>4 hours|<br><br>**Professional Services**<br>DR Consulting<br>+61 2 8218 2550<br>services@veeam.com<br>4 hours|<br><br>**Professional Services**<br>DR Consulting<br>+61 2 8218 2550<br>services@veeam.com<br>4 hours|<br><br>**Professional Services**<br>DR Consulting<br>+61 2 8218 2550<br>services@veeam.com<br>4 hours|
**Dedicated Support for DR Operations:**

**Customer Success Manager:** [Helen Mitchell - +61 404 XXX 130 - h.mitchell@veeam.com](mailto:h.mitchell@veeam.com)
**Senior Support Engineer:** [Christopher Evans - +61 404 XXX 131 - c.evans@veeam.com](mailto:c.evans@veeam.com)
**DR Architect:** [Susan Campbell - +61 404 XXX 132 - s.campbell@veeam.com](mailto:s.campbell@veeam.com)### **D.2 Database Vendors (Mission-Critical Support)**

**D.2.1 Oracle Corporation**
|Service Level|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**Premier Support**|Database Critical|1800 555 815|oracle.support@oracle.com|1 hour||**Security Response**|Security Patches|1800 555 815|security-alert@oracle.com|15 minutes||<br><br>**RAC Support**<br>Cluster Specialists<br>1800 555 815<br>rac-support@oracle.com<br>30 minutes|<br><br>**RAC Support**<br>Cluster Specialists<br>1800 555 815<br>rac-support@oracle.com<br>30 minutes|<br><br>**RAC Support**<br>Cluster Specialists<br>1800 555 815<br>rac-support@oracle.com<br>30 minutes|<br><br>**RAC Support**<br>Cluster Specialists<br>1800 555 815<br>rac-support@oracle.com<br>30 minutes|<br><br>**RAC Support**<br>Cluster Specialists<br>1800 555 815<br>rac-support@oracle.com<br>30 minutes|
**DR-Focused Support Team:**

**Account Manager:** [James Wilson - +61 404 XXX 140 - j.wilson@oracle.com](mailto:j.wilson@oracle.com)
**RAC Specialist:** [Catherine Brown - +61 404 XXX 141 - c.brown@oracle.com](mailto:c.brown@oracle.com)|D.2.2 SAP Australia|Col2|Col3|Col4|Col5||---|---|---|---|---||**Service Level**|**Contact Type**|**Phone**|**Email**|**Response Time**||**Enterprise Support**|SAP BASIS/HANA|1800 308 855|support@sap.com|1 hour||**HANA Premium**|In-Memory DB|1800 308 855|hana-support@sap.com|30 minutes||<br><br>**Mission Critical**<br>Emergency Response<br>1800 308 855<br>mission-critical@sap.com<br>15 minutes|<br><br>**Mission Critical**<br>Emergency Response<br>1800 308 855<br>mission-critical@sap.com<br>15 minutes|<br><br>**Mission Critical**<br>Emergency Response<br>1800 308 855<br>mission-critical@sap.com<br>15 minutes|<br><br>**Mission Critical**<br>Emergency Response<br>1800 308 855<br>mission-critical@sap.com<br>15 minutes|<br><br>**Mission Critical**<br>Emergency Response<br>1800 308 855<br>mission-critical@sap.com<br>15 minutes|
**Key Contacts:**

**Customer Success Partner:** [Daniel Garcia - +61 404 XXX 150 - d.garcia@sap.com](mailto:d.garcia@sap.com)
**HANA Architect:** [Sophie Mitchell - +61 404 XXX 151 - s.mitchell@sap.com](mailto:s.mitchell@sap.com)### **D.3 Cloud Application Vendors**

**D.3.1 Salesforce (Customer Service Platform)**
|Service Level|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**Premier Support**|24x7 Technical|+61 1800 667 638|premier@salesforce.com|1 hour||<br><br>**Mission Critical**<br>P1 Issues<br>+61 1800 667 638<br>critical@salesforce.com<br>30 minutes|<br><br>**Mission Critical**<br>P1 Issues<br>+61 1800 667 638<br>critical@salesforce.com<br>30 minutes|<br><br>**Mission Critical**<br>P1 Issues<br>+61 1800 667 638<br>critical@salesforce.com<br>30 minutes|<br><br>**Mission Critical**<br>P1 Issues<br>+61 1800 667 638<br>critical@salesforce.com<br>30 minutes|<br><br>**Mission Critical**<br>P1 Issues<br>+61 1800 667 638<br>critical@salesforce.com<br>30 minutes|
**D.3.2 Workday (HR Systems)**
|Service Level|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**Premium Support**|HR Systems|+61 2 8224 8200|support@workday.com|2 hours||<br><br>**Emergency Support**<br>Payroll Critical<br>+61 2 8224 8200<br>emergency@workday.com<br>1 hour|<br><br>**Emergency Support**<br>Payroll Critical<br>+61 2 8224 8200<br>emergency@workday.com<br>1 hour|<br><br>**Emergency Support**<br>Payroll Critical<br>+61 2 8224 8200<br>emergency@workday.com<br>1 hour|<br><br>**Emergency Support**<br>Payroll Critical<br>+61 2 8224 8200<br>emergency@workday.com<br>1 hour|<br><br>**Emergency Support**<br>Payroll Critical<br>+61 2 8224 8200<br>emergency@workday.com<br>1 hour|### **D.4 AI Chatbot Technology Partners**

**D.4.1 Microsoft (Teams Bot Integration)**
|Service|Contact Type|Phone|Email|Availability||---|---|---|---|---||**Bot Framework**<br>**Support**|Developer<br>Support|+61 1800 197<br>503|botframework@microsoft.com|Business<br>hours||<br><br>**Cognitive Services**<br>AI Platform<br>+61 1800 197<br>503<br>cognitive@microsoft.com<br>24x7|<br><br>**Cognitive Services**<br>AI Platform<br>+61 1800 197<br>503<br>cognitive@microsoft.com<br>24x7|<br><br>**Cognitive Services**<br>AI Platform<br>+61 1800 197<br>503<br>cognitive@microsoft.com<br>24x7|<br><br>**Cognitive Services**<br>AI Platform<br>+61 1800 197<br>503<br>cognitive@microsoft.com<br>24x7|<br><br>**Cognitive Services**<br>AI Platform<br>+61 1800 197<br>503<br>cognitive@microsoft.com<br>24x7||D.4.2 OpenAI (GPT|Integration)|Col3|Col4|Col5||---|---|---|---|---||**Service**|**Contact Type**|**Phone**|**Email**|**Availability**||**Enterprise API**|Technical Support|+1 415 555 0199|enterprise@openai.com|Business hours||<br><br>**Safety & Security**<br>Security Team<br>+1 415 555 0199<br>safety@openai.com<br>24x7|<br><br>**Safety & Security**<br>Security Team<br>+1 415 555 0199<br>safety@openai.com<br>24x7|<br><br>**Safety & Security**<br>Security Team<br>+1 415 555 0199<br>safety@openai.com<br>24x7|<br><br>**Safety & Security**<br>Security Team<br>+1 415 555 0199<br>safety@openai.com<br>24x7|<br><br>**Safety & Security**<br>Security Team<br>+1 415 555 0199<br>safety@openai.com<br>24x7|### **D.5 Telecommunications & Connectivity**

**D.5.1 Telstra Corporation (Primary WAN Provider)**
|Service|Contact Type|Phone|Email|Response Time||---|---|---|---|---||**Enterprise NOC**|Network Operations|132 200|noc@telstra.com|24x7||**Account Management**|Enterprise Team|132 200|enterprise@telstra.com|Business hours||<br><br>**Emergency Response**<br>Critical Outages<br>000<br>emergency@telstra.com<br>Immediate|<br><br>**Emergency Response**<br>Critical Outages<br>000<br>emergency@telstra.com<br>Immediate|<br><br>**Emergency Response**<br>Critical Outages<br>000<br>emergency@telstra.com<br>Immediate|<br><br>**Emergency Response**<br>Critical Outages<br>000<br>emergency@telstra.com<br>Immediate|<br><br>**Emergency Response**<br>Critical Outages<br>000<br>emergency@telstra.com<br>Immediate|
**Sarah Chen's Account Team:**

**Enterprise Account Manager:** [Ryan Clarke - +61 404 XXX 160 - r.clarke@telstra.com](mailto:r.clarke@telstra.com)
**Network Architect:** [Alice Chen - +61 404 XXX 161 - a.chen@telstra.com](mailto:a.chen@telstra.com)
**NOC Escalation Contact:** [Patrick O'Connor - +61 404 XXX 162 - p.oconnor@telstra.com](mailto:p.oconnor@telstra.com)## **APPENDIX E: NIST CONTROLS MAPPING MATRIX**
### **E.1 NIST SP 800-53 Rev 5 Controls Implementation**
|Control Family|Control<br>ID|Control Name|ACME<br>Implementation|Policy<br>Section|Validation<br>Method||---|---|---|---|---|---||**Access Control**<br>**(AC)**|AC-2|Account<br>Management|Role-based backup<br>access with MFA|Section<br>6.2|Quarterly access<br>reviews||**Access Control**<br>**(AC)**|AC-4|Information Flow<br>Enforcement|Network<br>segmentation for<br>backup traffic|Section<br>5.1.2|Monthly network<br>audits||**Access Control**<br>**(AC)**|AC-6|Least Privilege|Minimal backup<br>operator permissions|Section<br>6.2|Semi-annual<br>privilege reviews||**Audit and**<br>**Accountability**<br>**(AU)**|AU-2|Event Logging|All backup/restore<br>activities logged|Section<br>12.3|Daily log analysis||**Audit and**<br>**Accountability**<br>**(AU)**|AU-3|Content of Audit<br>Records|Detailed audit trail<br>with timestamps|Section<br>12.3|Weekly audit<br>verification||**Audit and**<br>**Accountability**<br>**(AU)**|AU-4|Audit Log<br>Storage Capacity|7-year audit log<br>retention|Section<br>12.3|Monthly capacity<br>monitoring||**Audit and**<br>**Accountability**<br>**(AU)**|AU-6|Audit Record<br>Review|Regular log review for<br>anomalies|Section<br>12.3|Daily automated<br>analysis||**Audit and**<br>**Accountability**<br>**(AU)**|AU-9|Protection of<br>Audit<br>Information|Encrypted and<br>tamper-proof logs|Section<br>12.3|Quarterly integrity<br>checks||**Contingency**<br>**Planning (CP)**|CP-2|Contingency Plan|Comprehensive<br>backup and DR policy|Section 1|Annual plan<br>review||Control Family|Control<br>ID|Control Name|ACME<br>Implementation|Policy<br>Section|Validation<br>Method||---|---|---|---|---|---||**Contingency**<br>**Planning (CP)**|CP-4|Contingency Plan<br>Testing|Monthly, quarterly,<br>and annual testing|Section 9|Test result<br>documentation||**Contingency**<br>**Planning (CP)**|CP-6|Alternate Storage<br>Site|Geographic<br>distribution of<br>backups|Section<br>5.1.2|Quarterly site<br>verification||**Contingency**<br>**Planning (CP)**|CP-7|Alternate<br>Processing Site|Melbourne secondary<br>data center|Section<br>5.1.2|Semi-annual DR<br>testing||**Contingency**<br>**Planning (CP)**|CP-9|Information<br>System Backup|Comprehensive<br>backup procedures|Section<br>5.2|Daily backup<br>verification||**Contingency**<br>**Planning (CP)**|CP-10|Information<br>System Recovery|Detailed recovery<br>procedures|Section<br>11.2|Monthly recovery<br>testing|### **E.2 NIST Cybersecurity Framework 2.0 Implementation**
|Function|Category|Subcategory|ACME<br>Implementation|Responsible<br>Party|Measurement||---|---|---|---|---|---||**GOVERN**<br>**(GV)**|GV.OC|Organizational<br>Cybersecurity<br>Strategy|Executive oversight<br>of backup strategy|CIO, DR<br>Manager|Quarterly reviews||**GOVERN**<br>**(GV)**|GV.RM|Risk Management<br>Strategy|Risk-based backup<br>classification|DR Manager,<br>Security|Annual risk<br>assessment||**IDENTIFY**<br>**(ID)**|ID.AM|Asset Management|Critical application<br>inventory|DR Manager,<br>IT Teams|Monthly inventory<br>updates||**IDENTIFY**<br>**(ID)**|ID.RA|Risk Assessment|Business impact<br>analysis for systems|DR Manager,<br>Business|Annual BIA<br>updates||**PROTECT**<br>**(PR)**|PR.AC|Identity<br>Management and<br>Access Control|Multi-factor<br>authentication for<br>backup systems|Security Team|Monthly access<br>audits||**PROTECT**<br>**(PR)**|PR.DS|Data Security|Encryption of all<br>backup data|Security, DR<br>Teams|Daily encryption<br>verification||**PROTECT**<br>**(PR)**|PR.IP|Information<br>Protection Processes|Backup procedures<br>and documentation|DR Manager|Quarterly<br>procedure review||**PROTECT**<br>**(PR)**|PR.MA|Maintenance|Regular backup<br>system maintenance|IT Operations|Weekly<br>maintenance logs||**PROTECT**<br>**(PR)**|PR.PT|Protective<br>Technology|Backup software and<br>infrastructure|DR Manager,<br>IT Ops|Monthly<br>performance<br>monitoring||**DETECT**<br>**(DE)**|DE.AE|Anomalies and<br>Events|Backup failure<br>detection and<br>alerting|DR Manager,<br>Monitoring|Real-time<br>monitoring||Function|Category|Subcategory|ACME<br>Implementation|Responsible<br>Party|Measurement||---|---|---|---|---|---||**DETECT**<br>**(DE)**|DE.CM|Security Continuous<br>Monitoring|SIEM integration for<br>backup systems|Security, DR<br>Teams|24x7 monitoring||**RESPOND**<br>**(RS)**|RS.RP|Response Planning|Incident response<br>procedures|DR Manager|Quarterly tabletop<br>exercises||**RESPOND**<br>**(RS)**|RS.CO|Communications|Stakeholder<br>notification<br>procedures|DR Manager,<br>Comms|Monthly<br>communication<br>tests||**RESPOND**<br>**(RS)**|RS.AN|Analysis|Incident analysis and<br>forensics|Security, DR<br>Teams|Post-incident<br>reviews||**RESPOND**<br>**(RS)**|RS.MI|Mitigation|Incident containment<br>procedures|DR Manager,<br>IT Ops|Incident response<br>exercises||**RESPOND**<br>**(RS)**|RS.IM|Improvements|Lessons learned<br>integration|DR Manager|Quarterly<br>improvement<br>reviews||**RECOVER**<br>**(RC)**|RC.RP|Recovery Planning|Comprehensive<br>recovery procedures|DR Manager|Monthly recovery<br>testing||**RECOVER**<br>**(RC)**|RC.IM|Recovery Plan<br>Implementation|Execution of recovery<br>procedures|DR Manager,<br>IT Teams|Recovery exercise<br>validation||**RECOVER**<br>**(RC)**|RC.CO|Recovery<br>Communications|Stakeholder updates<br>during recovery|DR Manager,<br>Comms|Communication<br>plan testing|### **E.3 ISO 27001:2022 Annex A Controls Mapping**
|Control|Control Title|ACME Implementation|Evidence Required||---|---|---|---||**A.5.2**|Information security roles and<br>responsibilities|Defined DR team roles and<br>responsibilities|Role documentation,<br>training records||**A.5.7**|Threat intelligence|Integration with security<br>monitoring|Threat intelligence reports||**A.8.9**|Access management|Role-based access for backup<br>systems|Access control matrix, audit<br>logs||**A.8.10**|Information in processing<br>systems|Data classification and handling|Data classification<br>procedures||**A.8.24**|Use of cryptography|Encryption of backup data|Encryption verification<br>reports||**A.12.3**|Information backup|Comprehensive backup<br>procedures|Backup policy, test results||**A.17.1**|Information security continuity|Business continuity planning|BCP documentation, test<br>results||<br><br>**A.17.2**<br>Redundancies<br>Geographic distribution of<br>backups<br>Site verification, failover<br>tests|<br><br>**A.17.2**<br>Redundancies<br>Geographic distribution of<br>backups<br>Site verification, failover<br>tests|<br><br>**A.17.2**<br>Redundancies<br>Geographic distribution of<br>backups<br>Site verification, failover<br>tests|<br><br>**A.17.2**<br>Redundancies<br>Geographic distribution of<br>backups<br>Site verification, failover<br>tests|### **E.4 Australian Privacy Principles (APP) Compliance**
|APP|Principle|ACME Implementation|Validation Method||---|---|---|---||**APP 1**|Open and transparent privacy policy|Customer data backup<br>transparency|Privacy policy<br>updates||**APP 3**|Collection of solicited personal<br>information|Data classification in backups|Collection notices||**APP 5**|Notification of collection|Backup data inclusion notices|Documentation<br>review||**APP 6**|Use or disclosure|Backup access controls|Access audit reports||**APP 8**|Cross-border disclosure|Australian data residency|Location verification||**APP**<br>**10**|Quality of personal information|Data integrity in backups|Quality assurance<br>tests||**APP**<br>**11**|Security of personal information|Encryption and access controls|Security assessments||**APP**<br>**12**|Access to personal information|Backup data access procedures|Access request logs||<br><br>**APP**<br>**13**<br>Correction of personal information<br>Data correction in backups<br>Correction<br>procedures|<br><br>**APP**<br>**13**<br>Correction of personal information<br>Data correction in backups<br>Correction<br>procedures|<br><br>**APP**<br>**13**<br>Correction of personal information<br>Data correction in backups<br>Correction<br>procedures|<br><br>**APP**<br>**13**<br>Correction of personal information<br>Data correction in backups<br>Correction<br>procedures|## **APPENDIX F: RECOVERY PROCEDURE CHECKLISTS (NIST CP-10 ALIGNED)**
### **F.1 AI Chatbot-Enabled Recovery Procedures (Sarah Chen's Toolkit)**

**F.1.1 AI Chatbot Integration for DR Operations**

**Voice Commands for DR Manager:**

"What's the status of SAP backup recovery?" - Real-time status updates
"Walk me through Oracle POS recovery steps" - Step-by-step guidance
"Who's the on-call DBA for emergency escalation?" - Contact information
"Generate Category 1 incident report for executive briefing" - Automated reporting
"Schedule DR test for Customer Loyalty Platform" - Test coordination
**Automated Workflows Triggered by Chatbot:**
1. **Incident Declaration:** "Declare Category 1 incident for SAP ERP failure"
2. **Team Assembly:** Automatic notification of DR specialists3. **Status Updates:** Real-time updates to executive team4. **Documentation:** Auto-generation of incident timeline5. **Post-Recovery:** Automated lessons learned compilation
**F.1.2 Category 1 Incident Response (Critical Systems)**

**Phase 1: Immediate Response (0-15 minutes) - NIST IR-4**

**AI Chatbot Activation:** "Initiate Category 1 DR response for [System Name]"
Automatic escalation to Sarah Chen (DR Manager)Notification to on-call technical teamsExecutive team alert (CIO, CISO, COO)ServiceNow incident ticket creation
**Initial Assessment (NIST CP-4)**
AI Chatbot queries: "What systems are affected?"
Business impact assessment: "What's the revenue impact?"
Customer impact evaluation: "How many stores affected?"
Recovery time estimation: "What's our expected RTO?"

**Phase 2: Recovery Coordination (15-60 minutes) - NIST CP-10**

**Recovery Team Assembly**
AI Chatbot: "Assemble DR team for [System Name] recovery"
Technical specialists notificationBusiness stakeholder communicationVendor escalation if required
**Recovery Execution Oversight**
AI Chatbot: "Start recovery procedures for [System Name]"
Real-time progress monitoringExecutive status updates every 30 minutesRisk assessment for additional system impacts
**Phase 3: Validation and Return to Service (60-120 minutes)**

**System Validation (NIST CP-10(6))**
AI Chatbot: "Run validation checklist for [System Name]"
Functional testing coordinationPerformance verificationSecurity controls validationIntegration testing with dependent systems
**Business Sign-off**
AI Chatbot: "Request business approval for [System Name] go-live"
User acceptance testing resultsBusiness stakeholder approvalCommunication to all affected users### **F.2 System-Specific Recovery Procedures**

**F.2.1 SAP ERP System Recovery (Mission-Critical)**

**AI Chatbot Guided Recovery Process:**

**Pre-Recovery Assessment (5 minutes)**

**Chatbot Query:** "Assess SAP ERP failure impact"
Check HANA database status and replicationVerify application server availabilityReview system logs and error messagesAssess financial reporting impact ($2M+ daily transactions)
**Recovery Execution (45-90 minutes)**

**Database Recovery (NIST CP-9)**
Chatbot: "Initiate HANA database recovery procedures"
Stop all SAP application servicesExecute HANA system replication failoverValidate database consistency and performanceVerify HANA tenant databases
**Application Layer Recovery (NIST CP-10)**
Chatbot: "Start SAP application server recovery"
Restore application server configurationsStart central services and message serverInitialize work processes and RFC connectionsVerify system landscape connectivity
**Business Validation (30 minutes)**

**Functional Testing**
Chatbot: "Execute SAP critical transaction tests"
Test Financial (FI) module transactionsVerify Materials Management (MM) processesCheck Sales & Distribution (SD) functionsValidate integration with POS systems
**Expected Recovery Timeline:** 2 hours (within RTO) **Success Criteria:** All critical business processesoperational
**F.2.2 Oracle POS System Recovery (Revenue-Critical)**

**Emergency Response (1-Hour RTO)**

**Immediate Actions (5 minutes)**

**Chatbot Alert:** "POS system failure detected - 450 stores affected"
Activate emergency cash-only procedures if neededNotify all regional store managersAssess payment gateway connectivityDetermine if customer loyalty system affected
**Recovery Execution (45 minutes)**

**Oracle RAC Failover (NIST CP-7)**
Chatbot: "Execute Oracle RAC failover to Melbourne site"
Initiate Data Guard switchover proceduresValidate database cluster connectivityVerify transaction log synchronizationTest POS terminal connectivity across all stores
**System Validation (NIST CP-10(6))**
Chatbot: "Run POS system validation checklist"
Test transaction processing capabilityVerify payment gateway integrationCheck customer loyalty program connectivityValidate receipt printing and cash drawer functions
**Return to Service (10 minutes)**

**Store Operations Coordination**
Chatbot: "Notify all stores - POS systems operational"
Coordinate with regional store managersResume normal payment processingMonitor transaction volumes and error ratesProvide executive status update
**F.2.3 Customer Loyalty Platform Recovery**

**Data Protection Priority Recovery (NIST SC-13)**

**Assessment Phase (10 minutes)**

**Privacy Impact Assessment**
Chatbot: "Assess customer data exposure risk"
Determine if personal information compromisedCheck encryption key integrityVerify access control enforcementAssess notification requirements under Privacy Act
**Recovery Phase (90 minutes)**

**SQL Server Always On Recovery**
Chatbot: "Initiate SQL Server failover procedures"
Execute Always On Availability Group failoverValidate data consistency across replicasVerify customer data integrity and completenessTest mobile app and POS integration
**Compliance Validation (30 minutes)**

**Privacy and Security Verification**
Chatbot: "Run privacy compliance checklist"
Verify encryption of customer personal dataValidate access controls and audit loggingCheck integration with marketing platformsConfirm notification procedures if data breach### **F.3 Infrastructure Recovery Procedures**

**F.3.1 Virtualization Infrastructure Recovery (NIST CP-6)**

**VMware vSphere Environment Recovery**

**Assessment and Planning**
Chatbot: "Assess VMware infrastructure failure scope"
Identify failed hosts and affected VMsCheck vCenter Server and ESXi host statusVerify shared storage availabilityReview Veeam backup repositories
**Recovery Execution**
Chatbot: "Execute VM instant recovery procedures"
Power off affected VMs if still accessibleInitiate Veeam Instant VM RecoveryVerify VM boot and OS functionalityTest application services and network connectivity
**Migration and Cleanup**
Storage vMotion from backup to production storageRemove temporary instant recovery objectsUpdate VM configuration and toolsResume normal backup schedules
**Expected Recovery Time:** 15-30 minutes per VM
**F.3.2 Network Infrastructure Recovery (NIST SC-7)**

**Critical Network Services Recovery**

**Immediate Response**
Chatbot: "Activate network redundancy protocols"
Verify failover to secondary network pathsCheck critical service connectivity (DNS, DHCP, AD)Coordinate with Telstra for WAN connectivity
**Recovery Actions**
Deploy replacement hardware if requiredRestore network configuration from backupsTest network segmentation and VLAN functionalityValidate security policies and firewall rules
**Service Restoration**
Gradually restore network trafficMonitor performance, latency, and packet lossUpdate network documentation and diagramsSchedule post-incident network assessment### **F.4 DR Manager Dashboard Metrics (Sarah Chen's KPIs)**

**F.4.1 Real-Time Recovery Metrics**
|Metric|Target|Current Status|Trend||---|---|---|---||**Recovery Time (RTO)**|<2 hours Tier 1|In Progress: 45 min|✅ On Track||**Data Loss (RPO)**|<15 min Tier 1|Last Backup: 8 min|✅ Within Target||**Team Response Time**|<15 min|Responded: 12 min|✅ Met Target||<br><br>**Business Impact**<br>Minimize<br>2 stores affected<br>⚠ Monitor|<br><br>**Business Impact**<br>Minimize<br>2 stores affected<br>⚠ Monitor|<br><br>**Business Impact**<br>Minimize<br>2 stores affected<br>⚠ Monitor|<br><br>**Business Impact**<br>Minimize<br>2 stores affected<br>⚠ Monitor|
**F.4.2 AI Chatbot Performance Metrics**
|Metric|Target|Current Performance||---|---|---||**Query Response Time**|<3 seconds|1.8 seconds average||**Procedure Accuracy**|99%|99.2% validation rate||**User Satisfaction**|>90%|94% positive feedback||<br><br>**Automation Success**<br>95%<br>97% successful workflows|<br><br>**Automation Success**<br>95%<br>97% successful workflows|<br><br>**Automation Success**<br>95%<br>97% successful workflows|### **F.5 Post-Recovery Validation Checklist**

**F.5.1 Technical Validation (NIST CP-10(6))**

**System Performance**
Database response times within baselineApplication performance metrics normalNetwork latency and throughput optimalStorage I/O performance validated
**Security Validation (NIST SC Family)**
Access controls functioning correctlyEncryption services operationalAudit logging capturing all activitiesSecurity monitoring alerts functional
**Integration Testing**
All system interfaces operationalData synchronization workingThird-party integrations functioningReal-time replication validated
**F.5.2 Business Validation**

**Operational Testing**
Critical business processes workingStore operations fully functionalCustomer services availableFinancial reporting operational
**Stakeholder Sign-off**
Business unit manager approvalRegional store manager confirmationIT operations team validationDR Manager final approval## **DOCUMENT CONTROL AND REVISION HISTORY**
### **Document Information**

**Total Pages:** 52
**Word Count:** ~18,000 words
**NIST Framework:** SP 800-53 Rev 5, CSF 2.0, SP 800-34 Rev 1 aligned
**Last Updated:** January 2025
**Next Review Date:** July 2025
**Classification:** Internal Use Only### **AI Chatbot Integration Notes**

**Designed for DR Manager Persona:** Sarah Chen, CBCP certified
**ServiceNow Integration:** Automated ticket creation and updates
**Microsoft Teams Bot:** Voice and text command interface
**Mobile App Support:** Field operations and store coordination
**Executive Reporting:** Automated status updates and dashboards### **Distribution List (NIST PM-1 Compliant)**
|Role|Name|Department|Access Level||---|---|---|---||CEO|Sarah Johnson|Executive|Read Only||CIO|Robert Taylor|IT|Full Access||**DR Manager**|**Sarah Chen**|**DR Operations**|**Full Edit Access**||Security Manager|Amanda Foster|Information Security|Read/Comment||Compliance Manager|Helen Mitchell|Risk & Compliance|Read/Comment||<br><br>All DR Specialists<br>DR Team<br>IT Operations<br>Read/Execute|<br><br>All DR Specialists<br>DR Team<br>IT Operations<br>Read/Execute|<br><br>All DR Specialists<br>DR Team<br>IT Operations<br>Read/Execute|<br><br>All DR Specialists<br>DR Team<br>IT Operations<br>Read/Execute|### **Revision History**
|Version|Date|Author|Changes||---|---|---|---||1.0|January 2025|IT Policy Team|Initial version with PCI requirements||<br><br>2.0<br>January 2025<br>DR Policy Team<br>NIST framework alignment, PCI removal, AI chatbot integration|<br><br>2.0<br>January 2025<br>DR Policy Team<br>NIST framework alignment, PCI removal, AI chatbot integration|<br><br>2.0<br>January 2025<br>DR Policy Team<br>NIST framework alignment, PCI removal, AI chatbot integration|<br><br>2.0<br>January 2025<br>DR Policy Team<br>NIST framework alignment, PCI removal, AI chatbot integration|
**Final Approval Required From:**
Chief Information Officer (Robert Taylor)Chief Executive Officer (Sarah Johnson)
**Disaster Recovery Manager (Sarah Chen)**  - Primary OwnerSecurity Manager (Amanda Foster)Compliance Manager (Helen Mitchell)_This document and all appendices contain confidential and proprietary information of ACME INC. This__NIST-aligned framework is specifically designed for DR Manager operations with AI chatbot__integration for enhanced incident response capabilities. Unauthorized distribution is prohibited._